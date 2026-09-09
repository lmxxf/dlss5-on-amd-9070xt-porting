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
#ifndef NATIVE_FP8_INPUT
#define NATIVE_FP8_INPUT 0
#endif
#if NATIVE_FP8_INPUT
// FAST PATH step 3: the producer (fused FFN contract / attention) already stored E4M3 bytes; load A tiles directly.
ByteAddressBuffer input:register(t0);
#else
StructuredBuffer<float> input:register(t0);
#endif
ByteAddressBuffer weights:register(t1);
#ifndef NATIVE_FP8_FEATURE
#define NATIVE_FP8_FEATURE 0
#endif
#ifndef NATIVE_FP8_STORE
#define NATIVE_FP8_STORE 0
#endif
#ifndef NATIVE_DIRECT_CAST
#define NATIVE_DIRECT_CAST 0
#endif
#ifndef NATIVE_MATRIX_RESIDUAL
#define NATIVE_MATRIX_RESIDUAL 0
#endif
#ifndef NATIVE_TILED_WEIGHTS
#define NATIVE_TILED_WEIGHTS 0
#endif
#if NATIVE_TILED_WEIGHTS && !NATIVE_FP8_OPERANDS
#error tiled weights are packed as E4M3 bytes
#endif
#if NATIVE_FP8_FEATURE
// FAST PATH: residual stream stored as E4M3 bytes.
ByteAddressBuffer feature8:register(t2);
float FromE4M3(uint b){uint e=(b>>3)&15u,m=b&7u;float v=e?asfloat(((e+120u)<<23)|(m<<20)):float(m)*0.001953125;return (b&0x80u)?-v:v;}
float FeatureAt(uint index){return FromE4M3((feature8.Load(index&~3u)>>((index&3u)*8u))&255u);}
#else
StructuredBuffer<float> feature:register(t2);
float FeatureAt(uint index){return feature[index];}
#endif
#if NATIVE_FP8_STORE
groupshared uint otile[BLOCK_N*64];
#endif
RWByteAddressBuffer output:register(u0);
#ifndef NATIVE_FP8_COPY
#define NATIVE_FP8_COPY 0
#endif
#if NATIVE_FP8_COPY
// FAST PATH (split copy8): also store the quantized output as E4M3 bytes ([token][MATRIX_CHANNELS]) so the C512 pack dispatch is skipped.
RWByteAddressBuffer copy8:register(u1);
#endif
cbuffer Geometry:register(b0){uint width;uint height;uint raster_width;uint raster_height;uint pad_x;uint pad_y;}
#ifndef MAP_FEATURE
#define MAP_FEATURE 0
#endif
#ifndef MAP_OUTPUT
#define MAP_OUTPUT 0
#endif
#if NATIVE_MATRIX_RESIDUAL && (!NATIVE_FP8_FEATURE || !NATIVE_FP8_OPERANDS)
#error NATIVE_MATRIX_RESIDUAL needs E4M3 residual bytes and FP8 operands
#endif
#if NATIVE_MATRIX_RESIDUAL && MAP_FEATURE
// MAP_FEATURE: the wave's 16 token rows (only its BLOCK_N*16 columns) are gathered by raster index into LDS first.
groupshared uint ftile8[BLOCK_N*64];
#endif
// Padded token -> raster index, or -1 for border tokens.
int raster_index(uint p){int x=int(p%width)-int(pad_x),y=int(p/width)-int(pad_y);if(x<0||y<0||x>=int(raster_width)||y>=int(raster_height))return -1;return int((uint(y)*raster_width+uint(x))*MATRIX_CHANNELS);}
#if NATIVE_FP8_OPERANDS
#define OPERAND dx::linalg::ComponentType::F8_E4M3FN
#define ELEM 1
groupshared uint tile8[128];
uint E4M3(float v){uint b=asuint(v),a=b&0x7fffffffu,sg=(b>>24)&0x80u;if(a==0)return sg;float m=abs(v);if(m<0.015625)return sg|uint(round(m*512.0));uint e=(a>>23)-127+7,mant=(a>>20)&7u;if(e>15)return sg|0x7eu;return sg|(e<<3)|mant;}
#else
#define OPERAND dx::linalg::ComponentType::F16
#define ELEM 2
groupshared float16_t tile[512];
#endif
#ifndef NATIVE_HW_H
#define NATIVE_HW_H 0
#endif
#if NATIVE_HW_H
// FAST PATH: f16 RNE through the hardware conversion pair (SM6 f32tof16 is round-to-nearest-even).
float H(float v){return f16tof32(f32tof16(v));}
#else
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
#endif
#if NATIVE_HW_H
// FAST PATH: bit-level RNE FP8 quantizer (same grid as the legacy F, no log2/exp2).
float F(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
#else
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
#endif
using A=dx::linalg::Matrix<OPERAND,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<OPERAND,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16;if(first>=width*height)return;
 C acc[BLOCK_N];
#if NATIVE_MATRIX_RESIDUAL
 // FAST PATH: residual feature*scale as MMAs. A = E4M3 feature tile of the 32-channel group, B = three E4M3
 // diagonal matrices whose sum is the f16 scale (packed by the host after the f32 scales).
 [unroll]for(uint n=0;n<BLOCK_N;n++)acc[n]=C::Splat(0.0f);
 [unroll]for(uint gg=0;gg<(BLOCK_N*16+31)/32;gg++){
  uint g=(gid.y*BLOCK_N*16)/32+gg;
#if MAP_FEATURE
  // Gather: 16 tokens x (BLOCK_N*16 bytes) = BLOCK_N*64 uints; each lane copies uint4 chunks (zeros for border tokens).
  if(gg==0){
   [unroll]for(uint kk=0;kk<BLOCK_N*16;kk+=32){uint k=kk+tid.x;uint row=k/BLOCK_N,q=k%BLOCK_N;int src=raster_index(first+row);uint4 v=src<0?uint4(0,0,0,0):feature8.Load4(uint(src)+gid.y*BLOCK_N*16+q*16);ftile8[row*(BLOCK_N*4)+q*4]=v.x;ftile8[row*(BLOCK_N*4)+q*4+1]=v.y;ftile8[row*(BLOCK_N*4)+q*4+2]=v.z;ftile8[row*(BLOCK_N*4)+q*4+3]=v.w;}
   GroupMemoryBarrierWithGroupSync();
  }
  A af=A::Load(ftile8,gg*8,BLOCK_N*4,dx::linalg::MatrixLayout::RowMajor);
#else
  A af=A::Load(feature8,first*MATRIX_CHANNELS+g*32,MATRIX_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
#endif
  [unroll]for(uint n=0;n<BLOCK_N;n++){
   uint col=(gid.y*BLOCK_N+n)*16;if(col/32!=g)continue;
   [unroll]for(uint p=0;p<3;p++){B s=B::Load(weights,MATRIX_CHANNELS*MATRIX_CHANNELS*ELEM+MATRIX_CHANNELS*4+((col/16)*3+p)*512,16,dx::linalg::MatrixLayout::RowMajor,16);acc[n].MultiplyAccumulate(af,s);}
  }
 }
#else
 [unroll]for(uint n=0;n<BLOCK_N;n++){
  uint col=(gid.y*BLOCK_N+n)*16;acc[n]=C::Splat(0.0f);
#if MAP_FEATURE
  for(uint i=0;i<acc[n].Length();i++){uint2 rc=acc[n].GetCoordinate(i);uint row=col+rc.y;int src=raster_index(first+rc.x);float f=src<0?0:FeatureAt(uint(src)+row);acc[n].Set(i,H(f*asfloat(weights.Load(MATRIX_CHANNELS*MATRIX_CHANNELS*ELEM+row*4))));}
#else
  for(uint i=0;i<acc[n].Length();i++){uint2 rc=acc[n].GetCoordinate(i);uint row=col+rc.y;acc[n].Set(i,H(FeatureAt((first+rc.x)*MATRIX_CHANNELS+row)*asfloat(weights.Load(MATRIX_CHANNELS*MATRIX_CHANNELS*ELEM+row*4))));}
#endif
 }
#endif
 [loop]for(uint g=0;g<MATRIX_CHANNELS/32;g++){
#if NATIVE_FP8_INPUT
  A a=A::Load(input,first*MATRIX_CHANNELS+g*32,MATRIX_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
#elif NATIVE_FP8_OPERANDS
  for(uint u=tid.x;u<128;u+=32){uint i0=u*4;uint row=(first+i0/32)*MATRIX_CHANNELS+g*32+i0%32;tile8[u]=E4M3(input[row])|(E4M3(input[row+1])<<8)|(E4M3(input[row+2])<<16)|(E4M3(input[row+3])<<24);}
  GroupMemoryBarrierWithGroupSync();
  A a=A::Load(tile8,0,8,dx::linalg::MatrixLayout::RowMajor);
#else
  for(uint i=tid.x;i<512;i+=32)tile[i]=float16_t(input[(first+i/32)*MATRIX_CHANNELS+g*32+i%32]);
  GroupMemoryBarrierWithGroupSync();
  A a=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
#endif
  [unroll]for(uint n=0;n<BLOCK_N;n++){
   uint col=(gid.y*BLOCK_N+n)*16;
#if NATIVE_TILED_WEIGHTS
   B b=B::Load(weights,((col/16)*(MATRIX_CHANNELS/32)+g)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
#else
   B b=B::Load(weights,(col*MATRIX_CHANNELS+g*32)*ELEM,MATRIX_CHANNELS*ELEM,dx::linalg::MatrixLayout::ColMajor,16);
#endif
#if NATIVE_FAST_ACCUMULATE
   acc[n].MultiplyAccumulate(a,b);
#else
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,H(acc[n].Get(i)+p.Get(i)));
#endif
  }
#if !NATIVE_FP8_INPUT
  GroupMemoryBarrierWithGroupSync();
#endif
 }
 [unroll]for(uint n=0;n<BLOCK_N;n++){
#if NATIVE_FAST_ACCUMULATE && NATIVE_FP8_STORE && NATIVE_DIRECT_CAST
  // FAST PATH: the E4M3 Cast below is the quantizer; skip the per-element F(H()) pass (drops the intermediate f16 rounding).
#elif NATIVE_FAST_ACCUMULATE
  for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,RAW?H(acc[n].Get(i)):F(H(acc[n].Get(i))));
#elif !RAW
  for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,F(acc[n].Get(i)));
#endif
#if NATIVE_FP8_STORE && MAP_OUTPUT
  // E4M3 tile staged in LDS (16 rows x 16 bytes = 4 uints per row), then 4-channel stores into the cropped raster.
  acc[n].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(otile,n*64,4,dx::linalg::MatrixLayout::RowMajor);
#elif NATIVE_FP8_STORE
  acc[n].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(output,first*MATRIX_CHANNELS+(gid.y*BLOCK_N+n)*16,MATRIX_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
#elif MAP_OUTPUT
  // Write the cropped raster directly; border tokens are dropped (the crop never read them).
  for(uint i=0;i<acc[n].Length();i++){uint2 rc=acc[n].GetCoordinate(i);int dst=raster_index(first+rc.x);if(dst>=0)output.Store(uint(dst+int((gid.y*BLOCK_N+n)*16+rc.y))*4,asuint(acc[n].Get(i)));}
#else
  acc[n].Store(output,(first*MATRIX_CHANNELS+(gid.y*BLOCK_N+n)*16)*4,MATRIX_CHANNELS*4,dx::linalg::MatrixLayout::RowMajor,16);
#if NATIVE_FP8_COPY
  acc[n].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(copy8,first*MATRIX_CHANNELS+(gid.y*BLOCK_N+n)*16,MATRIX_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
#endif
#endif
 }
#if NATIVE_FP8_STORE && MAP_OUTPUT
 GroupMemoryBarrierWithGroupSync();
 // BLOCK_N tiles x 16 rows x 4 uints = 256 uints; each lane stores 8, one row of one tile per pair of lanes.
 [unroll]for(uint k=0;k<BLOCK_N*64/32;k++){uint slot=k*32+tid.x,n=slot/64,row=(slot%64)/4,q=slot%4;int dst=raster_index(first+row);if(dst>=0)output.Store(uint(dst)+(gid.y*BLOCK_N+n)*16+q*4,otile[n*64+row*4+q]);}
#endif
}
