// FAST PATH: ViT global attention on E4M3 Q/K/V (the normalized values are on the F8 lattice, so the byte copy is exact).
// Scores stay in registers through the exp bit map; P is quantized by the hardware E4M3 cast into LDS ([query][key] bytes),
// the denominator accumulates through MMAs of the f16 exp tiles against all-ones, PV runs FP8 x FP8 with FP32 accumulation.
// Same values as native_wave_vit_attention_half.hlsl (NATIVE_FAST_VIT_ATTENTION) up to f32 summation order.
#include <dx/linalg.h>
ByteAddressBuffer qkv:register(t0);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint tokens;}
float H(float v){return f16tof32(f32tof16(v));}
float F(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
#define MAX_TOKENS 640
groupshared uint p8[16*MAX_TOKENS/4];
groupshared float16_t sc16[512];
groupshared float16_t ones16[512];
groupshared float inverse[16];
using A8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using AH=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using BH=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint t:SV_GroupIndex){
 uint query=gid.x*16,head=gid.y;const uint prow=MAX_TOKENS/4;
 for(uint i=t;i<512;i+=32)ones16[i]=float16_t(1.0);
 A8 q=A8::Load(qkv,query*1024+head*32,1024,dx::linalg::MatrixLayout::RowMajor,16);
 C rs=C::Splat(0.0f);
 for(uint key=0;key<tokens;key+=32){
  [unroll]for(uint j=0;j<2;j++){
   B8 k=B8::Load(qkv,(tokens+key+j*16)*1024+head*32,1024,dx::linalg::MatrixLayout::ColMajor,16);
   C s=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(q,k);
   for(uint i=0;i<s.Length();i++){float affine=clamp(s.Get(i)*f16tof32(0x2dbb)+1.708984375,1.439453125,1.9775390625);uint b=f32tof16(affine);s.Set(i,f16tof32(((b<<4)+0x4000)&65535));}
   s.Cast<dx::linalg::ComponentType::F16>().Store(sc16,j*16,32,dx::linalg::MatrixLayout::RowMajor);
   s.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(p8,(key+j*16)/4,prow,dx::linalg::MatrixLayout::RowMajor);
  }
  GroupMemoryBarrierWithGroupSync();
  AH st=AH::Load(sc16,0,32,dx::linalg::MatrixLayout::RowMajor);BH ones=BH::Load(ones16,0,16,dx::linalg::MatrixLayout::RowMajor);
  rs.MultiplyAccumulate(st,ones);
  GroupMemoryBarrierWithGroupSync();
 }
 for(uint i=0;i<rs.Length();i++){uint2 rc=rs.GetCoordinate(i);if(rc.y==0)inverse[rc.x]=1.0/rs.Get(i);}
 GroupMemoryBarrierWithGroupSync();
 C acc[2];acc[0]=C::Splat(0.0f);acc[1]=C::Splat(0.0f);
 for(uint key=0;key<tokens;key+=32){
  A8 pa=A8::Load(p8,key/4,prow,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint half_index=0;half_index<2;half_index++){
   B8 vb=B8::Load(qkv,(2*tokens+key)*1024+head*32+half_index*16,1024,dx::linalg::MatrixLayout::RowMajor,16);
   acc[half_index].MultiplyAccumulate(pa,vb);
  }
 }
 [unroll]for(uint half_index=0;half_index<2;half_index++)for(uint i=0;i<acc[half_index].Length();i++){
  uint2 c=acc[half_index].GetCoordinate(i);
  output[(query+c.x)*1024+head*32+half_index*16+c.y]=F(H(acc[half_index].Get(i)*inverse[c.x]));
 }
}
// f32 -> E4M3 copy of the normalized Q/K/V (F8-lattice values: exact; RNE cast is the identity on the lattice).
StructuredBuffer<float> source:register(t0);
RWByteAddressBuffer packed:register(u0);
uint E4M3(float v){uint b=asuint(v),a=b&0x7fffffffu,sg=(b>>24)&0x80u;if(a==0)return sg;float m=abs(v);if(m<0.015625)return sg|uint(round(m*512.0));uint e=(a>>23)-127+7,mant=(a>>20)&7u;if(e>15)return sg|0x7eu;return sg|(e<<3)|mant;}
[numthreads(64,1,1)]void pack8(uint3 id:SV_DispatchThreadID){
 uint quad=id.x;if(quad>=tokens*3072/4)return;
 packed.Store(quad*4,E4M3(source[quad*4])|(E4M3(source[quad*4+1])<<8)|(E4M3(source[quad*4+2])<<16)|(E4M3(source[quad*4+3])<<24));
}
