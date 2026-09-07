// Wave-matrix C x C projection with residual, identical numerics to
// native_c64.hlsl tiled_project: a = H(feature*scale); per 32-channel group
// a = H(a + sum); output = RAW ? a : F(a). Input values are F() outputs, so the
// f16 staging is exact; weights are exact halves (checked at init).
#ifndef MATRIX_CHANNELS
#define MATRIX_CHANNELS 64
#endif
#ifndef BLOCK_N
#define BLOCK_N 4
#endif
#ifndef RAW
#define RAW 0
#endif
#include <dx/linalg.h>
StructuredBuffer<float> input:register(t0);
ByteAddressBuffer weights:register(t1);
StructuredBuffer<float> feature:register(t2);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;}
groupshared float16_t tile[512];
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16;if(first>=width*height)return;
 C acc[BLOCK_N];
 [unroll]for(uint n=0;n<BLOCK_N;n++){
  uint col=(gid.y*BLOCK_N+n)*16;acc[n]=C::Splat(0.0f);
  for(uint i=0;i<acc[n].Length();i++){uint2 rc=acc[n].GetCoordinate(i);uint row=col+rc.y;acc[n].Set(i,H(feature[(first+rc.x)*MATRIX_CHANNELS+row]*asfloat(weights.Load(MATRIX_CHANNELS*MATRIX_CHANNELS*2+row*4))));}
 }
 [loop]for(uint g=0;g<MATRIX_CHANNELS/32;g++){
  for(uint i=tid.x;i<512;i+=32)tile[i]=float16_t(input[(first+i/32)*MATRIX_CHANNELS+g*32+i%32]);
  GroupMemoryBarrierWithGroupSync();
  A a=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint n=0;n<BLOCK_N;n++){
   uint col=(gid.y*BLOCK_N+n)*16;
   B b=B::Load(weights,(col*MATRIX_CHANNELS+g*32)*2,MATRIX_CHANNELS*2,dx::linalg::MatrixLayout::ColMajor,16);
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,H(acc[n].Get(i)+p.Get(i)));
  }
  GroupMemoryBarrierWithGroupSync();
 }
 [unroll]for(uint n=0;n<BLOCK_N;n++){
#if !RAW
  for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,F(acc[n].Get(i)));
#endif
  acc[n].Store(output,(first*MATRIX_CHANNELS+(gid.y*BLOCK_N+n)*16)*4,MATRIX_CHANNELS*4,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
