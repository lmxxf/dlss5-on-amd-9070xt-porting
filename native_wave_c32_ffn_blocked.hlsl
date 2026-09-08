// Register-blocked C32 FFN (32 -> 128 -> 32 with residual), same bindings and
// numerics as native_wave_c32_ffn_local.hlsl: identical K32 order, H() per step,
// F() on the hidden layer, initial residual H(input*scale). Only the LDS
// round trips are removed: expand keeps 8 accumulators and writes the f16
// hidden tile once; contract keeps 2 accumulators over 4 K steps.
#include <dx/linalg.h>
#if NATIVE_STATIC_LENGTH
// 16x16 f32 accumulator on wave32 = 8 elements per lane; static trip count lets Get/Set index registers statically.
#define ELEM_LOOP(m) [unroll]for(uint i=0;i<8;i++)
#else
#define ELEM_LOOP(m) for(uint i=0;i<m.Length();i++)
#endif
ByteAddressBuffer weights:register(t0);
StructuredBuffer<float> input:register(t1);
#if NATIVE_C32_FFN_FAST2
RWByteAddressBuffer output:register(u0);
#else
RWStructuredBuffer<float> output:register(u0);
#endif
cbuffer Geometry:register(b0){uint seed;uint width;uint height;uint local_oracle;uint temporal;
 // FAST PATH input mapping (NATIVE_C32_MAPPED_INPUT): 0 = tile-major work buffer as-is; 1 = HWC raster source;
 // 2 = previous stage's Main() (row-major over its own shifted work grid). Border tokens read zero.
 uint map_mode;uint src_width;uint src_height;uint shift_x;uint shift_y;uint prev_shift_x;uint prev_shift_y;uint prev_work_width;}
#ifndef NATIVE_C32_MAPPED_INPUT
#define NATIVE_C32_MAPPED_INPUT 0
#endif
#if NATIVE_C32_MAPPED_INPUT
int source_index(uint p){
 if(map_mode==0)return int(p*32);
 uint tile=p/64,x=(tile%(width/8))*8+p%8,y=(tile/(width/8))*8+(p%64)/8;
 int sx=int(x)-int(shift_x),sy=int(y)-int(shift_y);
 if(sx<0||sy<0||sx>=int(src_width)||sy>=int(src_height))return -1;
 if(map_mode==1)return int((uint(sy)*src_width+uint(sx))*32);
 // Main() of the previous stage is row-major over its (shifted) work grid.
 uint px=uint(sx)+prev_shift_x,py=uint(sy)+prev_shift_y;
 return int((py*prev_work_width+px)*32);
}
#endif
groupshared float16_t prefix[512],hidden[2048];
#ifndef NATIVE_C32_FFN_FAST2
#define NATIVE_C32_FFN_FAST2 0
#endif
#if NATIVE_C32_FFN_FAST2
groupshared float raw[512];
#endif
#ifndef NATIVE_C32_FFN_FP8
#define NATIVE_C32_FFN_FP8 0
#endif
#if NATIVE_C32_FFN_FP8
// FAST PATH: hidden layer as E4M3 through the hardware cast (no scalar Ffast); contract weights are an FP8 copy at byte 16512.
groupshared uint hidden8[512];
float ActivatePoly(float v){float g=clamp(v,-4.0,4.0),p=g*(abs(g)*(-.055908203125)+.447265625)+.89453125;return v*p;}
#endif
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
#if NATIVE_FAST_F
// Bit-level equivalent of the legacy F for finite inputs: round-to-nearest-even on the
// 3-bit mantissa (HLSL round() is RNE), same subnormal path, same 448 clamp.
float F(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
#else
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
#endif
#if NATIVE_FAST_EPILOGUE
// FAST PATH stage 3a: activation polynomial without intermediate f16 roundings; single RNE quantization to the FP8 grid.
float Ffast(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
float Activate(float v){float g=clamp(v,-4.0,4.0),p=g*(abs(g)*(-.055908203125)+.447265625)+.89453125;return Ffast(v*p);}
#endif
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=(gid.x+gid.y*65535u)*16,t=tid.x;if(first>=width*height)return;
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
#if NATIVE_C32_FFN_FAST2
 // FAST PATH: raw input tile kept in LDS for the residual; quantized copy for the A operand.
#if NATIVE_C32_MAPPED_INPUT
 [branch]if(map_mode==0){for(uint i=t;i<512;i+=32){float v=input[first*32+i];raw[i]=v;prefix[i]=float16_t(Ffast(v));}}
 else{
  // Lane j<16 resolves token j's source once; the staging loop reads it back with a uniform lane index.
  const int mine=t<16?source_index(first+t):0;
  [unroll]for(uint j=0;j<16;j++){int src=WaveReadLaneAt(mine,j);uint i=j*32+t;float v=src<0?0:input[uint(src)+t];raw[i]=v;prefix[i]=float16_t(Ffast(v));}
 }
#else
 for(uint i=t;i<512;i+=32){float v=input[first*32+i];raw[i]=v;prefix[i]=float16_t(Ffast(v));}
#endif
#else
 for(uint i=t;i<512;i+=32)prefix[i]=float16_t(F(input[first*32+i]));
#endif
 GroupMemoryBarrierWithGroupSync();
 {
  A a=A::Load(prefix,0,32,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint block=0;block<8;block++){
   B b=B::Load(weights,block*16*32*2,64,dx::linalg::MatrixLayout::ColMajor,16);
   C h=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
#if NATIVE_C32_FFN_FP8
   ELEM_LOOP(h)h.Set(i,ActivatePoly(h.Get(i)));
   h.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(hidden8,block*4,32,dx::linalg::MatrixLayout::RowMajor);
#elif NATIVE_C32_FFN_FAST2
   ELEM_LOOP(h)h.Set(i,Activate(h.Get(i)));
   h.Cast<dx::linalg::ComponentType::F16>().Store(hidden,block*16,128,dx::linalg::MatrixLayout::RowMajor);
#elif NATIVE_FAST_EPILOGUE
   ELEM_LOOP(h){uint2 rc=h.GetCoordinate(i);hidden[rc.x*128+block*16+rc.y]=float16_t(Activate(h.Get(i)));}
#else
   ELEM_LOOP(h){float v=H(h.Get(i)),g=clamp(v,-4.0,4.0),p=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);uint2 rc=h.GetCoordinate(i);hidden[rc.x*128+block*16+rc.y]=float16_t(F(H(v*p)));}
#endif
  }
 }
 GroupMemoryBarrierWithGroupSync();
 C acc[2];
 [unroll]for(uint block=0;block<2;block++){
  acc[block]=C::Splat(0.0f);
#if NATIVE_C32_FFN_FAST2
  ELEM_LOOP(acc[block]){uint2 rc=acc[block].GetCoordinate(i);uint c=block*16+rc.y;acc[block].Set(i,raw[rc.x*32+c]*asfloat(weights.Load(16384+c*4)));}
#else
  ELEM_LOOP(acc[block]){uint2 rc=acc[block].GetCoordinate(i);uint c=block*16+rc.y;acc[block].Set(i,H(input[(first+rc.x)*32+c]*asfloat(weights.Load(16384+c*4))));}
#endif
 }
 for(uint g=0;g<4;g++){
#if NATIVE_C32_FFN_FP8
  using A8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
  using B8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
  A8 a=A8::Load(hidden8,g*8,32,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint block=0;block<2;block++){
   B8 b=B8::Load(weights,16512+block*16*128+g*32,128,dx::linalg::MatrixLayout::ColMajor,16);
#else
  A a=A::Load(hidden,g*32,128,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint block=0;block<2;block++){
   B b=B::Load(weights,8192+(block*16*128+g*32)*2,256,dx::linalg::MatrixLayout::ColMajor,16);
#endif
#if NATIVE_FAST_ACCUMULATE
   acc[block].MultiplyAccumulate(a,b);
#else
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   ELEM_LOOP(acc[block])acc[block].Set(i,H(acc[block].Get(i)+p.Get(i)));
#endif
  }
 }
#if NATIVE_C32_FFN_FAST2
 // Output stays f32 (RAW) for the following stage; f16 rounding through a hardware cast pair.
 [unroll]for(uint block=0;block<2;block++){ELEM_LOOP(acc[block])acc[block].Set(i,f16tof32(f32tof16(acc[block].Get(i))));acc[block].Store(output,(first*32+block*16)*4,128,dx::linalg::MatrixLayout::RowMajor,16);}
}
#else
#if NATIVE_FAST_ACCUMULATE
 [unroll]for(uint block=0;block<2;block++)ELEM_LOOP(acc[block])acc[block].Set(i,H(acc[block].Get(i)));
#endif
 [unroll]for(uint block=0;block<2;block++)ELEM_LOOP(acc[block]){uint2 rc=acc[block].GetCoordinate(i);output[(first+rc.x)*32+block*16+rc.y]=acc[block].Get(i);}
}
#endif
