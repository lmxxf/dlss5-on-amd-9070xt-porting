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
#ifndef NATIVE_QKV_FUSED
#define NATIVE_QKV_FUSED 0
#endif
#if NATIVE_QKV_FUSED
// FAST PATH (ViT QKV fused): projection + per-head normalize + E4M3 store in one wave. BLOCK_N=4 columns = two heads, so
// the row sum of squares of a head is one MMA of the f16 squared tile pair against all-ones; q rows are scaled by
// 5.65625 * head scale (f32 copy appended to the packed weights at byte 6291456), v rows are stored as-is. The output is
// the E4M3 Q/K/V buffer the FP8 attention reads (no normalize dispatch, no pack8). f32 sums, no intermediate H().
groupshared float16_t sq16[512];
groupshared float16_t ones16[512];
[WaveSize(32)]
#ifndef NATIVE_VIT_TILED
#define NATIVE_VIT_TILED 0
#endif
[numthreads(32,1,1)]void project_fused(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16*BLOCK_M;if(first>=tokens)return;
 uint col0=gid.y*BLOCK_N*16,part=col0/1024,row0=col0%1024;
 for(uint i=tid.x;i<512;i+=32)ones16[i]=float16_t(1.0);
 C acc[BLOCK_M][BLOCK_N];
 [unroll]for(uint m=0;m<BLOCK_M;m++)[unroll]for(uint n=0;n<BLOCK_N;n++)acc[m][n]=C::Splat(0.0f);
 [loop]for(uint k=0;k<1024;k+=32){
#if NATIVE_VIT_TILED
  /* FAST PATH (DLSS5_VIT_TILED): pack16 output and the f16 weights as 1KB [16|k 32][32|j 16] tiles (no 2048-byte row strides). */
  A a[BLOCK_M];[unroll]for(uint m=0;m<BLOCK_M;m++)a[m]=A::Load(input16,(((first+m*16)/16)*32+k/32)*1024+(k%32)*2,64,dx::linalg::MatrixLayout::RowMajor,16);
  [unroll]for(uint n=0;n<BLOCK_N;n++){
   B b=B::Load(weights,part*2097152+(((row0+n*16)/16)*32+k/32)*1024,32,dx::linalg::MatrixLayout::RowMajor,16);
#else
  A a[BLOCK_M];[unroll]for(uint m=0;m<BLOCK_M;m++)a[m]=A::Load(input16,((first+m*16)*1024+k)*2,2048,dx::linalg::MatrixLayout::RowMajor,16);
  [unroll]for(uint n=0;n<BLOCK_N;n++){
   B b=B::Load(weights,(part*1048576+(row0+n*16)*1024+k)*2,2048,dx::linalg::MatrixLayout::ColMajor,16);
#endif
   [unroll]for(uint m=0;m<BLOCK_M;m++)acc[m][n].MultiplyAccumulate(a[m],b);
  }
 }
 GroupMemoryBarrierWithGroupSync();
 B ones=B::Load(ones16,0,16,dx::linalg::MatrixLayout::RowMajor);
 [unroll]for(uint m=0;m<BLOCK_M;m++)[unroll]for(uint hp=0;hp<BLOCK_N/2;hp++){
  if(part<2){
   [unroll]for(uint j=0;j<2;j++){C sq=acc[m][hp*2+j];for(uint i=0;i<sq.Length();i++){float v=sq.Get(i);sq.Set(i,v*v);}sq.Cast<dx::linalg::ComponentType::F16>().Store(sq16,j*16,32,dx::linalg::MatrixLayout::RowMajor);}
   GroupMemoryBarrier();
   A st=A::Load(sq16,0,32,dx::linalg::MatrixLayout::RowMajor);
   C rs=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(st,ones);
   float head_scale=part==0?5.65625*asfloat(weights.Load(6291456+((row0+hp*32)/32)*4)):1.0;
   [unroll]for(uint j=0;j<2;j++)for(uint i=0;i<rs.Length();i++)acc[m][hp*2+j].Set(i,acc[m][hp*2+j].Get(i)*(rsqrt(max(rs.Get(i),6.198883056640625e-5))*head_scale));
   GroupMemoryBarrier();
  }
  [unroll]for(uint j=0;j<2;j++)acc[m][hp*2+j].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(output,(part*tokens+first+m*16)*1024+row0+(hp*2+j)*16,1024,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
#elif BLOCK_M>1
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
