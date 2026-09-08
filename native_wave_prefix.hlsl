// FAST PATH: wave-matrix preblock input mix (16 features -> 32 prefix channels), replacing the per-pixel scalar
// preblock_input_mix.hlsl main (NATIVE_FAST_PREFIX + LIVE_PROFILE + DYNAMIC_PARAMETERS + NATIVE_TEMPORAL_RGB + NATIVE_PREFIX_ONLY).
// One wave = 16 pixels: lanes 0..15 build one pixel's 16 features each (same hashes / Box-Muller / half rounding as the
// scalar kernel) into an f16 tile (K padded to 32 with zeros); the [32 out][16 in] f32 weights are staged as two f16
// B tiles; two MMAs produce the 16x32 prefix; half_round per element, coalesced f32 stores. Same bindings as the scalar kernel.
#include <dx/linalg.h>
StructuredBuffer<float> weights:register(t0);
StructuredBuffer<float> input:register(t1);
StructuredBuffer<float4> temporal_rgb:register(t3);
RWStructuredBuffer<float> output:register(u0);
cbuffer RuntimeParameters:register(b0){uint runtime_seed;uint runtime_width;uint runtime_height;uint local_oracle;uint runtime_temporal_enabled;}
uint pcg(uint s){uint w=((s>>((s>>28)+4))^s)*0x108ef2d9;return (w>>22)^w;}
float uniform24(uint s){uint w=((s>>((s>>28)+4))^s)*0x108ef2d9;return float(((w>>30)^(w>>8))+1)*5.9604644775390625e-8;}
float half_round(float v){
 uint bits=asuint(v),sg=bits&0x80000000u,a=bits&0x7fffffffu;
 if(a>=0x7f800000u)return v;
 if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return (sg!=0)?-q:q;}
 uint rounded=(a+0xfffu+((a>>13)&1u))&0xffffe000u;
 if(rounded>=0x47800000u)return asfloat(sg|0x7f800000u);
 return asfloat(sg|rounded);
}
groupshared float16_t ftile[16*32];
groupshared float16_t btile[2*512];
groupshared float otile[16*32];
using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 const uint t=tid.x,first=(gid.x+gid.y*65535u)*16;if(first>=runtime_width*runtime_height)return;
 // B tiles: [n][k][j] = k<16 ? W[(n*16+j)*16+k] : 0 (f16-exact weights, checked on the host).
 for(uint i=t;i<1024;i+=32){uint n=i/512,k=(i%512)/16,j=i%16;btile[i]=float16_t(k<16?weights[(n*16+j)*16+k]:0.0);}
 if(t<16){
  const uint p=first+t;
  uint x=p%8,y=(p/8)%8;
  if(!local_oracle){uint tile=p/64;x=(tile%(runtime_width/8))*8+p%8;y=(tile/(runtime_width/8))*8+(p%64)/8;}
  uint h=pcg((x*0x8da6b343)^(y*0xd8163841)^(runtime_seed*0x9e3779b9u)^0x243f6a88u);
  float a=uniform24(h*0xcaa5b80d+0x21dd796b),b=uniform24(h*0x2c9277b5+0xac564b05);
  float c=uniform24(h*0x83232c31+0x3463e0ac),d=uniform24(h*0xfa6dc5f9+0x4712a88e);
  float r0=sqrt(-2*log(a)),r1=sqrt(-2*log(b));
  float g0=half_round(r0*cos(6.283185482025146*c));
  float g1=half_round(r1*cos(6.283185482025146*d));
  float g2=half_round(r1*sin(6.283185482025146*d));
  const float rgb_scale=.125;
  float r=half_round(half_round(half_round(input[p*4])-0.5)*rgb_scale);
  float g=half_round(half_round(half_round(input[p*4+1])-0.5)*rgb_scale);
  float bl=half_round(half_round(half_round(input[p*4+2])-0.5)*rgb_scale);
  float features[16]={g1,g2,g,bl,g0,1,.0078125,1,r,g,1,1,bl,r,1,0};
  if(runtime_temporal_enabled){
   uint tile=p/64;uint tx=(tile%(runtime_width/8))*8+p%8,ty=(tile/(runtime_width/8))*8+(p%64)/8;
   float3 history=temporal_rgb[ty*runtime_width+tx].xyz;
   features[13]=half_round(half_round(half_round(history.x)-.5)*.125);
   features[2]=half_round(half_round(half_round(history.y)-.5)*.125);
   features[3]=half_round(half_round(half_round(history.z)-.5)*.125);
  }
  [unroll]for(uint i=0;i<16;i++)ftile[t*32+i]=float16_t(features[i]);
  [unroll]for(uint i=16;i<32;i++)ftile[t*32+i]=float16_t(0.0);
 }
 GroupMemoryBarrierWithGroupSync();
 A fa=A::Load(ftile,0,32,dx::linalg::MatrixLayout::RowMajor);
 [unroll]for(uint n=0;n<2;n++){
  B wb=B::Load(btile,n*512,16,dx::linalg::MatrixLayout::RowMajor);
  C z=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(fa,wb);
  for(uint i=0;i<z.Length();i++)z.Set(i,half_round(z.Get(i)));
  z.Store(otile,n*16,32,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
 [unroll]for(uint i=0;i<16;i++)output[first*32+i*32+t]=otile[i*32+t];
}
