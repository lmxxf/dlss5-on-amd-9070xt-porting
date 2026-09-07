#ifndef MATRIX_CHANNELS
#define MATRIX_CHANNELS 512
#endif
#include <dx/linalg.h>
ByteAddressBuffer input:register(t0),weights:register(t1);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;uint source_width;uint valid_width;uint valid_height;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID){
 if(gid.x*16>=width*height)return;
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 C acc=C::Splat(0.0f);
 for(uint k=0;k<MATRIX_CHANNELS/32;k++){
  A a=A::Load(input,(gid.x*16*MATRIX_CHANNELS+k*32)*2,MATRIX_CHANNELS*2,dx::linalg::MatrixLayout::RowMajor,16);
  B b=B::Load(weights,(gid.y*16*MATRIX_CHANNELS+k*32)*2,MATRIX_CHANNELS*2,dx::linalg::MatrixLayout::ColMajor,16);
  C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
  for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)+p.Get(i)));
 }
 for(uint i=0;i<acc.Length();i++)acc.Set(i,F(acc.Get(i)));
 acc.Store(output,(gid.x*16*(2*MATRIX_CHANNELS)+gid.y*16)*4,2*MATRIX_CHANNELS*4,dx::linalg::MatrixLayout::RowMajor,16);
}
