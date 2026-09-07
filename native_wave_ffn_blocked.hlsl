// Register-blocked Swin FFN (expand + contract) for the wave-matrix path.
// Numerics are identical to native_wave_expand/contract.hlsl: every output
// element still receives the same K32 partial products in the same order with
// the same H() after each step. Only data movement changes:
//  * one wave computes 16 tokens x (16*BLOCK_N) outputs, so each A tile is
//    loaded once and reused for BLOCK_N weight tiles;
//  * the hidden activation is stored as f16 instead of f32. Hidden values are
//    F() outputs (FP8 grid), so the conversion is exact.
#ifndef MATRIX_CHANNELS
#define MATRIX_CHANNELS 256
#endif
#ifndef BLOCK_N
#define BLOCK_N 4
#endif
#include <dx/linalg.h>
ByteAddressBuffer input:register(t0),weights:register(t1);
cbuffer Geometry:register(b0){uint width;uint height;}
RWByteAddressBuffer output:register(u0);
#define HIDDEN (4*MATRIX_CHANNELS)
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;

// input: packed f16 activations [tokens][C]; output: f16 hidden [tokens][4C].
[WaveSize(32)]
[numthreads(32,1,1)]void expand(uint3 gid:SV_GroupID){
 if(gid.x*16>=width*height)return;
 C acc[BLOCK_N];
 [loop]for(uint g=0;g<MATRIX_CHANNELS/32;g++){
  A a=A::Load(input,(gid.x*16*MATRIX_CHANNELS+g*32)*2,MATRIX_CHANNELS*2,dx::linalg::MatrixLayout::RowMajor,16);
  [unroll]for(uint n=0;n<BLOCK_N;n++){
   uint col=(gid.y*BLOCK_N+n)*16;
   B b=B::Load(weights,(col*MATRIX_CHANNELS+g*32)*2,MATRIX_CHANNELS*2,dx::linalg::MatrixLayout::ColMajor,16);
   C partial=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   if(g==0){acc[n]=partial;for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,H(acc[n].Get(i)));}
   else for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,H(acc[n].Get(i)+partial.Get(i)));
  }
 }
 [unroll]for(uint n=0;n<BLOCK_N;n++){
  uint col=(gid.y*BLOCK_N+n)*16;
  for(uint i=0;i<acc[n].Length();i++){
   float v=acc[n].Get(i),g=clamp(v,-4.0,4.0),p=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);
   acc[n].Set(i,F(H(v*p)));
  }
#if NATIVE_SCATTER_STORE
  for(uint i=0;i<acc[n].Length();i++){uint2 rc=acc[n].GetCoordinate(i);output.Store<float16_t>(((gid.x*16+rc.x)*HIDDEN+col+rc.y)*2,float16_t(acc[n].Get(i)));}
#else
  // Values are on the FP8 grid, so the f16 cast is exact; the matrix store writes coalesced rows.
  acc[n].Cast<dx::linalg::ComponentType::F16>().Store(output,(gid.x*16*HIDDEN+col)*2,HIDDEN*2,dx::linalg::MatrixLayout::RowMajor,16);
#endif
 }
}

// input: f16 hidden [tokens][4C]; output: f32 [tokens][C] (unchanged contract
// output format, consumed by the projection pass).
[WaveSize(32)]
[numthreads(32,1,1)]void contract(uint3 gid:SV_GroupID){
 if(gid.x*16>=width*height)return;
 C acc[BLOCK_N];
 [unroll]for(uint n=0;n<BLOCK_N;n++)acc[n]=C::Splat(0.0f);
 [loop]for(uint g=0;g<HIDDEN/32;g++){
  A a=A::Load(input,(gid.x*16*HIDDEN+g*32)*2,HIDDEN*2,dx::linalg::MatrixLayout::RowMajor,16);
  [unroll]for(uint n=0;n<BLOCK_N;n++){
   uint col=(gid.y*BLOCK_N+n)*16;
   B b=B::Load(weights,HIDDEN*MATRIX_CHANNELS*2+(col*HIDDEN+g*32)*2,HIDDEN*2,dx::linalg::MatrixLayout::ColMajor,16);
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,H(acc[n].Get(i)+p.Get(i)));
  }
 }
 [unroll]for(uint n=0;n<BLOCK_N;n++){
  for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,F(acc[n].Get(i)));
  acc[n].Store(output,(gid.x*16*MATRIX_CHANNELS+(gid.y*BLOCK_N+n)*16)*4,MATRIX_CHANNELS*4,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
