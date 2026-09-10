// FAST PATH (DLSS5_FUSED_FFN_PROJ0): Swin FFN expand + contract + FFN output projection (with residual) in one dispatch.
// The expand/contract half is native_wave_ffn_fused.hlsl unchanged; the contract tiles are cast to E4M3 into LDS instead
// of VRAM, and after one group sync each wave computes one 16x16 tile of the projection exactly as
// native_wave_project.hlsl (MAP_FEATURE, FP8 input, no matrix residual, direct cast store): acc = H(feature*scale) per
// element, then the K32 MMAs in order, then the E4M3 cast into result[0]. Same operations, same order: bit for bit.
// Built with NATIVE_PRECISE_CHAIN=1 (no FMA contraction in the activation polynomial); the reference chain is built
// with the same macro, because contraction of that polynomial is context dependent on this driver.
#ifndef MATRIX_CHANNELS
#define MATRIX_CHANNELS 256
#endif
#include <dx/linalg.h>
ByteAddressBuffer input:register(t0),weights:register(t1);
#ifndef NATIVE_FP8_FEATURE
#define NATIVE_FP8_FEATURE 0
#endif
#if NATIVE_FP8_FEATURE
ByteAddressBuffer feature8:register(t2);
float FromE4M3(uint b){uint e=(b>>3)&15u,m=b&7u;float v=e?asfloat(((e+120u)<<23)|(m<<20)):float(m)*0.001953125;return (b&0x80u)?-v:v;}
float FeatureAt(uint index){return FromE4M3((feature8.Load(index&~3u)>>((index&3u)*8u))&255u);}
#else
StructuredBuffer<float> feature:register(t2);
float FeatureAt(uint index){return feature[index];}
#endif
ByteAddressBuffer pweights:register(t3); /* projection weights of native_wave_project (E4M3 [C][C] or tiled, then f32 scales) */
cbuffer Geometry:register(b0){uint width;uint height;uint raster_width;uint raster_height;uint pad_x;uint pad_y;}
RWByteAddressBuffer output:register(u0);
#define HIDDEN (4*MATRIX_CHANNELS)
#define WAVES (MATRIX_CHANNELS/16)
#ifndef NATIVE_TILED_WEIGHTS
#define NATIVE_TILED_WEIGHTS 0
#endif
#ifndef NATIVE_TILED_PQ
#define NATIVE_TILED_PQ 0
#endif
#ifndef NATIVE_PRECISE_CHAIN
#define NATIVE_PRECISE_CHAIN 0
#endif
#define OPERAND dx::linalg::ComponentType::F8_E4M3FN
float H(float v){return f16tof32(f32tof16(v));}
float Ffast(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
#if NATIVE_PRECISE_CHAIN
float Activate(float v){float g=clamp(v,-4.0,4.0);precise float q=abs(g)*(-.055908203125)+.447265625;precise float p=g*q+.89453125;return Ffast(v*p);}
#else
float Activate(float v){float g=clamp(v,-4.0,4.0),p=g*(abs(g)*(-.055908203125)+.447265625)+.89453125;return Ffast(v*p);}
#endif
using A=dx::linalg::Matrix<OPERAND,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<OPERAND,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
groupshared uint hidden[16*HIDDEN/4];
/* the contract output [16][C] E4M3 reuses the hidden block after a group sync (LDS stays 16*4C bytes, same occupancy as native_wave_ffn_fused) */
#define ffn8 hidden
int raster_index(uint p){int x=int(p%width)-int(pad_x),y=int(p/width)-int(pad_y);if(x<0||y<0||x>=int(raster_width)||y>=int(raster_height))return -1;return int((uint(y)*raster_width+uint(x))*MATRIX_CHANNELS);}
[WaveSize(32)]
[numthreads(32*WAVES,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 if(gid.x*16>=width*height)return;
 const uint wave=tid.x/32,first=gid.x*16;
 /* projection residual a = H(feature*scale), gathered by raster index first so the loads overlap the FFN (border tokens read zero) */
 C pacc;
 {const uint col=wave*16;for(uint i=0;i<pacc.Length();i++){uint2 rc=pacc.GetCoordinate(i);uint row=col+rc.y;int src=raster_index(first+rc.x);float f=src<0?0:FeatureAt(uint(src)+row);pacc.Set(i,H(f*asfloat(pweights.Load(MATRIX_CHANNELS*MATRIX_CHANNELS+row*4))));}}
 {
  C acc[4];
  [loop]for(uint g=0;g<MATRIX_CHANNELS/32;g++){
   A a=A::Load(input,first*MATRIX_CHANNELS+g*32,MATRIX_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
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
   for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,Activate(acc[n].Get(i)));
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
  for(uint i=0;i<acc.Length();i++)acc.Set(i,Ffast(H(acc.Get(i))));
  GroupMemoryBarrierWithGroupSync(); /* every wave is done reading the hidden block */
  acc.Cast<OPERAND>().Store(ffn8,col/4,MATRIX_CHANNELS/4,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
 {
  /* projection: this wave's 16 output channels on top of the residual */
  const uint col=wave*16;C acc=pacc;
  [loop]for(uint g=0;g<MATRIX_CHANNELS/32;g++){
   A a=A::Load(ffn8,g*32/4,MATRIX_CHANNELS/4,dx::linalg::MatrixLayout::RowMajor);
#if NATIVE_TILED_PQ
   B b=B::Load(pweights,((col/16)*(MATRIX_CHANNELS/32)+g)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
#else
   B b=B::Load(pweights,col*MATRIX_CHANNELS+g*32,MATRIX_CHANNELS,dx::linalg::MatrixLayout::ColMajor,16);
#endif
   acc.MultiplyAccumulate(a,b);
  }
  acc.Cast<OPERAND>().Store(output,first*MATRIX_CHANNELS+col,MATRIX_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
