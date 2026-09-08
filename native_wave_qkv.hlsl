// FAST PATH: wave-matrix QKV projection (C -> 3C) from packed E4M3 activations and E4M3 weights,
// hardware FP32 accumulation, one H() at the end (replaces the thread-scope f16 matvec).
#ifndef MATRIX_CHANNELS
#define MATRIX_CHANNELS 64
#endif
#include <dx/linalg.h>
ByteAddressBuffer input:register(t0),weights:register(t1);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
using A=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID){
 if(gid.x*16>=width*height)return;
 C acc[4];
 [loop]for(uint g=0;g<MATRIX_CHANNELS/32;g++){
  A a=A::Load(input,gid.x*16*MATRIX_CHANNELS+g*32,MATRIX_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
  [unroll]for(uint n=0;n<4;n++){
   uint col=(gid.y*4+n)*16; // 0..3C-1, weights row-major [3C][C] in part order
   B b=B::Load(weights,col*MATRIX_CHANNELS+g*32,MATRIX_CHANNELS,dx::linalg::MatrixLayout::ColMajor,16);
   if(g==0)acc[n]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);else acc[n].MultiplyAccumulate(a,b);
  }
 }
 [unroll]for(uint n=0;n<4;n++){
  uint col=(gid.y*4+n)*16,part=col/MATRIX_CHANNELS,row=col%MATRIX_CHANNELS;
  for(uint i=0;i<acc[n].Length();i++){uint2 rc=acc[n].GetCoordinate(i);output[((gid.x*16+rc.x)*3+part)*MATRIX_CHANNELS+row+rc.y]=H(acc[n].Get(i));}
 }
}
