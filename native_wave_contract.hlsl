#ifndef MATRIX_CHANNELS
#define MATRIX_CHANNELS 256
#endif
#include <dx/linalg.h>
StructuredBuffer<float> input:register(t0);
ByteAddressBuffer weights:register(t1);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;}
#if NATIVE_PRELOAD_CONTRACT
groupshared float16_t tile[16*4*MATRIX_CHANNELS];
#else
groupshared float16_t tile[512];
#endif
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 if(gid.x*16>=width*height)return;
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 C acc=C::Splat(0.0f);
#if NATIVE_PRELOAD_CONTRACT
 for(uint i=tid.x;i<16*4*MATRIX_CHANNELS;i+=32)tile[i]=float16_t(input[gid.x*16*4*MATRIX_CHANNELS+i]);
 GroupMemoryBarrierWithGroupSync();
#endif
 [loop]for(uint g=0;g<4*MATRIX_CHANNELS/32;g++){
#if NATIVE_PRELOAD_CONTRACT
  A a=A::Load(tile,g*32,4*MATRIX_CHANNELS,dx::linalg::MatrixLayout::RowMajor);
#else
  for(uint i=tid.x;i<512;i+=32)tile[i]=float16_t(input[(gid.x*16+i/32)*(4*MATRIX_CHANNELS)+g*32+i%32]);
  GroupMemoryBarrierWithGroupSync();
  A a=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
#endif
  B b=B::Load(weights,4*MATRIX_CHANNELS*MATRIX_CHANNELS*2+(gid.y*16*(4*MATRIX_CHANNELS)+g*32)*2,4*MATRIX_CHANNELS*2,dx::linalg::MatrixLayout::ColMajor,16);
  C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
  for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)+p.Get(i)));
#if !NATIVE_PRELOAD_CONTRACT
  GroupMemoryBarrierWithGroupSync();
#endif
 }
 for(uint i=0;i<acc.Length();i++)acc.Set(i,F(acc.Get(i)));
 acc.Store(output,(gid.x*16*MATRIX_CHANNELS+gid.y*16)*4,MATRIX_CHANNELS*4,dx::linalg::MatrixLayout::RowMajor,16);
}
