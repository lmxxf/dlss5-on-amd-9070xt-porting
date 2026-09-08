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
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint seed;uint width;uint height;uint local_oracle;uint temporal;}
groupshared float16_t prefix[512],hidden[2048];
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
#if NATIVE_FAST_F
// Bit-level equivalent of the legacy F for finite inputs: round-to-nearest-even on the
// 3-bit mantissa (HLSL round() is RNE), same subnormal path, same 448 clamp.
float F(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
#else
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
#endif
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=(gid.x+gid.y*65535u)*16,t=tid.x;if(first>=width*height)return;
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 for(uint i=t;i<512;i+=32)prefix[i]=float16_t(F(input[first*32+i]));
 GroupMemoryBarrierWithGroupSync();
 {
  A a=A::Load(prefix,0,32,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint block=0;block<8;block++){
   B b=B::Load(weights,block*16*32*2,64,dx::linalg::MatrixLayout::ColMajor,16);
   C h=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   ELEM_LOOP(h){float v=H(h.Get(i)),g=clamp(v,-4.0,4.0),p=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);uint2 rc=h.GetCoordinate(i);hidden[rc.x*128+block*16+rc.y]=float16_t(F(H(v*p)));}
  }
 }
 GroupMemoryBarrierWithGroupSync();
 C acc[2];
 [unroll]for(uint block=0;block<2;block++){
  acc[block]=C::Splat(0.0f);
  ELEM_LOOP(acc[block]){uint2 rc=acc[block].GetCoordinate(i);uint c=block*16+rc.y;acc[block].Set(i,H(input[(first+rc.x)*32+c]*asfloat(weights.Load(16384+c*4))));}
 }
 for(uint g=0;g<4;g++){
  A a=A::Load(hidden,g*32,128,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint block=0;block<2;block++){
   B b=B::Load(weights,8192+(block*16*128+g*32)*2,256,dx::linalg::MatrixLayout::ColMajor,16);
#if NATIVE_FAST_ACCUMULATE
   acc[block].MultiplyAccumulate(a,b);
#else
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   ELEM_LOOP(acc[block])acc[block].Set(i,H(acc[block].Get(i)+p.Get(i)));
#endif
  }
 }
#if NATIVE_FAST_ACCUMULATE
 [unroll]for(uint block=0;block<2;block++)ELEM_LOOP(acc[block])acc[block].Set(i,H(acc[block].Get(i)));
#endif
 [unroll]for(uint block=0;block<2;block++)ELEM_LOOP(acc[block]){uint2 rc=acc[block].GetCoordinate(i);output[(first+rc.x)*32+block*16+rc.y]=acc[block].Get(i);}
}
