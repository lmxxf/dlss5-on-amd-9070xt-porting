// Wave-matrix ViT QKV projection (1024 -> 3x1024), numerically identical to
// native_vit_qkv.hlsl project: two K=512 groups, H() after every K32 step,
// total = H(group0 + group1). Each wave computes 16 tokens x (16*BLOCK_N) rows.
#ifndef BLOCK_N
#define BLOCK_N 4
#endif
#include <dx/linalg.h>
#ifndef NATIVE_PACKED_INPUT
#define NATIVE_PACKED_INPUT 0
#endif
#if NATIVE_PACKED_INPUT
// FAST PATH: f16 operand copy of the input; A tiles loaded straight from memory.
ByteAddressBuffer input16:register(t0);
#else
StructuredBuffer<float> input:register(t0);
#endif
ByteAddressBuffer weights:register(t1);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint tokens;}
groupshared float16_t tile[512];
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
#ifndef BLOCK_M
#define BLOCK_M 1
#endif
#if BLOCK_M>1
// FAST PATH: BLOCK_M token tiles per wave share every weight tile load. Packed f16 input only.
[WaveSize(32)]
[numthreads(32,1,1)]void project(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16*BLOCK_M;if(first>=tokens)return;
 uint col0=gid.y*BLOCK_N*16,part=col0/1024,row0=col0%1024;
 C acc[BLOCK_M][BLOCK_N];
 [unroll]for(uint m=0;m<BLOCK_M;m++)[unroll]for(uint n=0;n<BLOCK_N;n++)acc[m][n]=C::Splat(0.0f);
 [loop]for(uint k=0;k<1024;k+=32){
  A a[BLOCK_M];[unroll]for(uint m=0;m<BLOCK_M;m++)a[m]=A::Load(input16,((first+m*16)*1024+k)*2,2048,dx::linalg::MatrixLayout::RowMajor,16);
  [unroll]for(uint n=0;n<BLOCK_N;n++){
   B b=B::Load(weights,(part*1048576+(row0+n*16)*1024+k)*2,2048,dx::linalg::MatrixLayout::ColMajor,16);
   [unroll]for(uint m=0;m<BLOCK_M;m++)acc[m][n].MultiplyAccumulate(a[m],b);
  }
 }
 [unroll]for(uint m=0;m<BLOCK_M;m++)[unroll]for(uint n=0;n<BLOCK_N;n++){
  for(uint i=0;i<acc[m][n].Length();i++)acc[m][n].Set(i,H(acc[m][n].Get(i)));
  acc[m][n].Store(output,((part*tokens+first+m*16)*1024+row0+n*16)*4,4096,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
#else
[WaveSize(32)]
[numthreads(32,1,1)]void project(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16;if(first>=tokens)return;
 uint col0=gid.y*BLOCK_N*16,part=col0/1024,row0=col0%1024;
 C total[BLOCK_N];
 [loop]for(uint group=0;group<2;group++){
  C acc[BLOCK_N];
  [unroll]for(uint n=0;n<BLOCK_N;n++)acc[n]=C::Splat(0.0f);
  [loop]for(uint k=group*512;k<(group+1)*512;k+=32){
#if NATIVE_PACKED_INPUT
   A a=A::Load(input16,(first*1024+k)*2,2048,dx::linalg::MatrixLayout::RowMajor,16);
#else
   for(uint i=tid.x;i<512;i+=32)tile[i]=float16_t(input[(first+i/32)*1024+k+i%32]);
   GroupMemoryBarrierWithGroupSync();
   A a=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
#endif
   [unroll]for(uint n=0;n<BLOCK_N;n++){
    B b=B::Load(weights,(part*1048576+(row0+n*16)*1024+k)*2,2048,dx::linalg::MatrixLayout::ColMajor,16);
#if NATIVE_FAST_ACCUMULATE
    acc[n].MultiplyAccumulate(a,b);
#else
    C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
    for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,H(acc[n].Get(i)+p.Get(i)));
#endif
   }
#if !NATIVE_PACKED_INPUT
   GroupMemoryBarrierWithGroupSync();
#endif
  }
#if NATIVE_FAST_ACCUMULATE
  [unroll]for(uint n=0;n<BLOCK_N;n++){if(group==0)total[n]=acc[n];else for(uint i=0;i<total[n].Length();i++)total[n].Set(i,total[n].Get(i)+acc[n].Get(i));}
#else
  [unroll]for(uint n=0;n<BLOCK_N;n++){if(group==0)total[n]=acc[n];else for(uint i=0;i<total[n].Length();i++)total[n].Set(i,H(total[n].Get(i)+acc[n].Get(i)));}
#endif
 }
#if NATIVE_FAST_ACCUMULATE
 [unroll]for(uint n=0;n<BLOCK_N;n++)for(uint i=0;i<total[n].Length();i++)total[n].Set(i,H(total[n].Get(i)));
#endif
 [unroll]for(uint n=0;n<BLOCK_N;n++)total[n].Store(output,((part*tokens+first)*1024+row0+n*16)*4,4096,dx::linalg::MatrixLayout::RowMajor,16);
}
#endif
