// FAST PATH (plan step 2b): Swin FFN expand + contract in one dispatch.
// One group = 16 tokens, WAVES = C/16 waves. Expand: each wave produces 4 hidden tiles
// (16 tokens x 64 hidden) and stores them as E4M3 into LDS (16 x 4C bytes). Contract:
// each wave produces one output tile (16 tokens x 16 channels) from the LDS hidden block.
// The 4C-wide hidden tensor never touches VRAM. Numerics = blocked FFN fast path
// (FP32 accumulation, Activate() epilogue, F(H()) contract epilogue).
#ifndef MATRIX_CHANNELS
#define MATRIX_CHANNELS 256
#endif
#include <dx/linalg.h>
ByteAddressBuffer input:register(t0),weights:register(t1);
cbuffer Geometry:register(b0){uint width;uint height;}
RWByteAddressBuffer output:register(u0);
#define HIDDEN (4*MATRIX_CHANNELS)
#define WAVES (MATRIX_CHANNELS/16)
#ifndef NATIVE_TILED_WEIGHTS
#define NATIVE_TILED_WEIGHTS 0
#endif
#define OPERAND dx::linalg::ComponentType::F8_E4M3FN
#ifndef NATIVE_HW_H
#define NATIVE_HW_H 0
#endif
#if NATIVE_HW_H
// FAST PATH: f16 RNE through the hardware conversion pair (SM6 f32tof16 is round-to-nearest-even).
float H(float v){return f16tof32(f32tof16(v));}
#else
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
#endif
float Ffast(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
#ifndef NATIVE_HW_QUANTIZE
#define NATIVE_HW_QUANTIZE 0
#endif
#ifndef NATIVE_PRECISE_CHAIN
#define NATIVE_PRECISE_CHAIN 0
#endif
#if NATIVE_PRECISE_CHAIN
// FAST PATH (DLSS5_BUILD_C32_PRECISE_CHAIN): no FMA contraction in the polynomial (context dependent on this driver), so native_wave_ffn_proj0_fused.hlsl matches bit for bit.
float ActivatePoly(float v){float g=clamp(v,-4.0,4.0);precise float q=abs(g)*(-.055908203125)+.447265625;precise float p=g*q+.89453125;return v*p;}
float Activate(float v){return Ffast(ActivatePoly(v));}
#else
float ActivatePoly(float v){float g=clamp(v,-4.0,4.0),p=g*(abs(g)*(-.055908203125)+.447265625)+.89453125;return v*p;}
float Activate(float v){float g=clamp(v,-4.0,4.0),p=g*(abs(g)*(-.055908203125)+.447265625)+.89453125;return Ffast(v*p);}
#endif
using A=dx::linalg::Matrix<OPERAND,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<OPERAND,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
// Hidden block [16][HIDDEN] E4M3, four bytes per uint; matrix Load/Store index in uints.
groupshared uint hidden[16*HIDDEN/4];
[WaveSize(32)]
[numthreads(32*WAVES,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 if(gid.x*16>=width*height)return;
 const uint wave=tid.x/32;
 {
  C acc[4];
  [loop]for(uint g=0;g<MATRIX_CHANNELS/32;g++){
   A a=A::Load(input,gid.x*16*MATRIX_CHANNELS+g*32,MATRIX_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
   [unroll]for(uint n=0;n<4;n++){
    uint col=(wave*4+n)*16;
#if NATIVE_TILED_WEIGHTS
    B b=B::Load(weights,((col/16)*(MATRIX_CHANNELS/32)+g)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
#else
    B b=B::Load(weights,col*MATRIX_CHANNELS+g*32,MATRIX_CHANNELS,dx::linalg::MatrixLayout::ColMajor,16);
#endif
    if(g==0)acc[n]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);else acc[n].MultiplyAccumulate(a,b);
   }
  }
  [unroll]for(uint n=0;n<4;n++){
   uint col=(wave*4+n)*16;
#if NATIVE_HW_QUANTIZE
   // Polynomial only; the E4M3 cast below is the (hardware RNE) quantization.
   for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,ActivatePoly(acc[n].Get(i)));
#else
   for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,Activate(acc[n].Get(i)));
#endif
   acc[n].Cast<OPERAND>().Store(hidden,col/4,HIDDEN/4,dx::linalg::MatrixLayout::RowMajor);
  }
 }
 GroupMemoryBarrierWithGroupSync();
 {
  C acc=C::Splat(0.0f);
  const uint col=wave*16;
  [loop]for(uint g=0;g<HIDDEN/32;g++){
   A a=A::Load(hidden,g*32/4,HIDDEN/4,dx::linalg::MatrixLayout::RowMajor);
#if NATIVE_TILED_WEIGHTS
   B b=B::Load(weights,HIDDEN*MATRIX_CHANNELS+((col/16)*(HIDDEN/32)+g)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
#else
   B b=B::Load(weights,HIDDEN*MATRIX_CHANNELS+col*HIDDEN+g*32,HIDDEN,dx::linalg::MatrixLayout::ColMajor,16);
#endif
   acc.MultiplyAccumulate(a,b);
  }
#if !(NATIVE_HW_QUANTIZE&&NATIVE_FP8_OUTPUT)
  for(uint i=0;i<acc.Length();i++)acc.Set(i,Ffast(H(acc.Get(i))));
#endif
#if NATIVE_FP8_OUTPUT
  // FAST PATH step 3: values are on the FP8 grid; store E4M3 bytes for the projection's direct A load.
  acc.Cast<OPERAND>().Store(output,gid.x*16*MATRIX_CHANNELS+col,MATRIX_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
#else
  acc.Store(output,(gid.x*16*MATRIX_CHANNELS+col)*4,MATRIX_CHANNELS*4,dx::linalg::MatrixLayout::RowMajor,16);
#endif
 }
}
