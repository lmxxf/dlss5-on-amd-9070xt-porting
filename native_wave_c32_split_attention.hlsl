// C32 window attention split into four passes with no LDS staging of Q/K/V.
// Tokens are already window-major (64 consecutive tokens per 8x8 window).
// Numerics match preblock_attention_four_wave.hlsl exactly:
//   qkv:        z=H(F(input) x W) per K32 tile; q/k stored raw, v stored F(z).
//   normalize:  qi/ki from the NativeHalfSquarePair tree; qn=F(H(H(q*qi)*H(scale))), kn=F(H(k*ki)).
//   attention:  score=H(qk+bias), fast_exp bit map, denominator tree, F(H(ex*inv)),
//               acc=H(p0*v0); acc=H(acc+p1*v1); out=F(acc).
//   projection: z=attn x Wp (single K32, no H); result=half_add_preserving_midpoint(z,H(input*rs)).
// Storage: raw  = f16 [token][64]  (q 32, k 32)         -- same byte size as f32[token][32]
//          main = f16 [token][32] v  then f16 [token][32] attention output (second half of main)
// weights = packed layout of native_preblock_runtime (see NATIVE_C32_LOCAL_WEIGHTS).
#ifndef RAW_OUTPUT
#define RAW_OUTPUT 1
#endif
#include <dx/linalg.h>
ByteAddressBuffer weights:register(t0);
StructuredBuffer<float> input:register(t1);
RWByteAddressBuffer qk:register(u0);
RWByteAddressBuffer aux:register(u1);
cbuffer RuntimeGeometry:register(b0){uint runtime_seed;uint runtime_width;uint runtime_height;uint local_oracle;uint temporal_enabled;}
#ifndef NATIVE_HW_H
#define NATIVE_HW_H 0
#endif
#if NATIVE_HW_H
// FAST PATH: f16 RNE through the hardware conversion pair (SM6 f32tof16 is round-to-nearest-even).
float H(float v){return f16tof32(f32tof16(v));}
#else
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
#endif
#if NATIVE_HW_H
// FAST PATH: bit-level RNE FP8 quantizer (same grid as the legacy F, no log2/exp2).
float F(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
#else
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
#endif
#include "native_half_square.hlsli"
#define W_BIAS(i) asfloat(weights.Load(8192+(i)*4))
#define W_SCALE asfloat(weights.Load(24576))
#define W_RESIDUAL(c) asfloat(weights.Load(24580+(c)*4))
float half_add_preserving_midpoint(float a,float b){
 precise float sum=a+b;
 precise float virtual_b=sum-a;
 precise float error=(a-(sum-virtual_b))+(b-virtual_b);
 uint bits=asuint(sum),magnitude=bits&0x7fffffffu;
 if(magnitude>=0x38800000u&&magnitude<0x47800000u&&(magnitude&0x1fffu)==0x1000u&&error!=0){
  bool increase_bits=(error>0)==(sum>0);
  sum=asfloat(increase_bits?bits+1:bits-1);
 }
 return H(sum);
}
float fast_exp(float x){float a=clamp(H(x*.044921875+1.30078125),1.03125,1.5693359375);uint b=f32tof16(a);return f16tof32(((b<<5)+0x8000u)&65535u);}
#if NATIVE_FAST_ATTENTION
// FAST PATH stage 3b: scalar segments without intermediate f16 roundings.
float Ffast(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
float fast_exp_raw(float x){float a=clamp(x*.044921875+1.30078125,1.03125,1.5693359375);uint b=f32tof16(a);return f16tof32(((b<<5)+0x8000u)&65535u);}
#endif
using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
uint attn_base(){return runtime_width*runtime_height*32*2;} // byte offset of the attention output inside aux

#ifndef NATIVE_C32_FUSED
#define NATIVE_C32_FUSED 0
#endif
#if PASS==0
groupshared float16_t tile[512];
#if NATIVE_C32_FUSED
// FAST PATH: qkv + normalize in one wave. Row square sums via LDS, q/k scaled and
// FP8-quantized in registers, stored as f16 with coalesced matrix stores.
groupshared float squares[2*512];
groupshared float inv[2*16];
#endif
[WaveSize(32)]
[numthreads(32,1,1)]void qkv(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=(gid.x+gid.y*65535u)*16;if(first>=runtime_width*runtime_height)return;
 for(uint i=tid.x;i<512;i+=32)tile[i]=float16_t(F(input[first*32+i]));
 GroupMemoryBarrierWithGroupSync();
 A a=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
#if NATIVE_C32_FUSED
 C z[6];
 [unroll]for(uint part=0;part<3;part++)[unroll]for(uint cr=0;cr<2;cr++){
  B b=B::Load(weights,(part*1024+cr*16*32)*2,64,dx::linalg::MatrixLayout::ColMajor,16);
  z[part*2+cr]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
 }
 [unroll]for(uint h=0;h<2;h++)[unroll]for(uint cr=0;cr<2;cr++)for(uint i=0;i<z[h*2+cr].Length();i++){uint2 rc=z[h*2+cr].GetCoordinate(i);float v=z[h*2+cr].Get(i);squares[h*512+rc.x*32+cr*16+rc.y]=v*v;}
 GroupMemoryBarrierWithGroupSync();
 {const uint h=tid.x/16,r=tid.x%16;float ss=0;[unroll]for(uint c=0;c<32;c++)ss+=squares[h*512+r*32+c];inv[h*16+r]=rsqrt(max(ss,6.198883056640625e-5))*(h==0?W_SCALE:1);}
 GroupMemoryBarrierWithGroupSync();
 [unroll]for(uint part=0;part<3;part++)[unroll]for(uint cr=0;cr<2;cr++){
  C t=z[part*2+cr];
#if NATIVE_C32_FP8_QKV
  // Q/K/V as E4M3 bytes in aux [token][96] (q 32, k 32, v 32) through the hardware cast; qk buffer is left for the output.
  if(part<2)for(uint i=0;i<t.Length();i++){uint2 rc=t.GetCoordinate(i);t.Set(i,t.Get(i)*inv[part*16+rc.x]);}
  t.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(aux,first*96+part*32+cr*16,96,dx::linalg::MatrixLayout::RowMajor,16);
#else
  if(part<2){for(uint i=0;i<t.Length();i++){uint2 rc=t.GetCoordinate(i);t.Set(i,Ffast(t.Get(i)*inv[part*16+rc.x]));}t.Cast<dx::linalg::ComponentType::F16>().Store(qk,(first*64+part*32+cr*16)*2,128,dx::linalg::MatrixLayout::RowMajor,16);}
  else{for(uint i=0;i<t.Length();i++)t.Set(i,Ffast(H(t.Get(i))));t.Cast<dx::linalg::ComponentType::F16>().Store(aux,(first*32+cr*16)*2,64,dx::linalg::MatrixLayout::RowMajor,16);}
#endif
 }
#else
 [unroll]for(uint part=0;part<3;part++)[unroll]for(uint cr=0;cr<2;cr++){
  B b=B::Load(weights,(part*1024+cr*16*32)*2,64,dx::linalg::MatrixLayout::ColMajor,16);
  C z=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
  if(part<2){for(uint i=0;i<z.Length();i++)z.Set(i,H(z.Get(i)));z.Cast<dx::linalg::ComponentType::F16>().Store(qk,(first*64+part*32+cr*16)*2,128,dx::linalg::MatrixLayout::RowMajor,16);}
  else{for(uint i=0;i<z.Length();i++)z.Set(i,F(H(z.Get(i))));z.Cast<dx::linalg::ComponentType::F16>().Store(aux,(first*32+cr*16)*2,64,dx::linalg::MatrixLayout::RowMajor,16);}
 }
#endif
}
#elif PASS==1
[WaveSize(32)]
[numthreads(32,1,1)]void normalize(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint p=gid.x+gid.y*65535u;if(p>=runtime_width*runtime_height)return;uint c=tid.x;
 float q=float(qk.Load<float16_t>((p*64+c)*2)),k=float(qk.Load<float16_t>((p*64+32+c)*2));
#if NATIVE_FAST_ATTENTION
 float q0=WaveActiveSum(q*q),k0=WaveActiveSum(k*k);
 float qi=rsqrt(max(q0,6.198883056640625e-5)),ki=rsqrt(max(k0,6.198883056640625e-5)),scale=W_SCALE;
 qk.Store<float16_t>((p*64+c)*2,float16_t(Ffast(q*qi*scale)));
 qk.Store<float16_t>((p*64+32+c)*2,float16_t(Ffast(k*ki)));
#else
 float qs=NativeHalfSquarePair(q,WaveReadLaneAt(q,c+16)),ks=NativeHalfSquarePair(k,WaveReadLaneAt(k,c+16));
 {float a=WaveReadLaneAt(qs,c*2),b=WaveReadLaneAt(qs,c*2+1);float ka=WaveReadLaneAt(ks,c*2),kb=WaveReadLaneAt(ks,c*2+1);qs=H(a+b);ks=H(ka+kb);}
 [unroll]for(uint step=4;step>0;step/=2){float a=WaveReadLaneAt(qs,c+step),b=WaveReadLaneAt(ks,c+step);qs=H(qs+a);ks=H(ks+b);}
 float q0=WaveReadLaneAt(qs,0),k0=WaveReadLaneAt(ks,0);
 float qi=H(rsqrt(max(q0,6.198883056640625e-5))),ki=H(rsqrt(max(k0,6.198883056640625e-5))),scale=H(W_SCALE);
 qk.Store<float16_t>((p*64+c)*2,float16_t(F(H(H(q*qi)*scale))));
 qk.Store<float16_t>((p*64+32+c)*2,float16_t(F(H(k*ki))));
#endif
}
#elif PASS==2
groupshared float16_t ex[4096];
groupshared float softmax_inverse[64];
#ifndef NATIVE_C32_FUSED
#define NATIVE_C32_FUSED 0
#endif
#if NATIVE_C32_FUSED
groupshared float16_t attn[64*32];
#endif
#ifndef NATIVE_C32_FP8_QKV
#define NATIVE_C32_FP8_QKV 0
#endif
#if NATIVE_C32_FP8_QKV
using A8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
groupshared uint p8[1024];
uint E4M3(float v){uint b=asuint(v),a=b&0x7fffffffu,sg=(b>>24)&0x80u;if(a==0)return sg;float m=abs(v);if(m<0.015625)return sg|uint(round(m*512.0));uint e=(a>>23)-127+7,mant=(a>>20)&7u;if(e>15)return sg|0x7eu;return sg|(e<<3)|mant;}
#endif
[WaveSize(32)]
[numthreads(256,1,1)]void attention(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint window=gid.x,t=tid.x;if(window*64>=runtime_width*runtime_height)return;
 uint qr=t/64,col_start=(t/32)&1,base=window*64;
 {
  C s[2];
#if NATIVE_C32_FP8_QKV
  A8 qa=A8::Load(aux,(base+qr*16)*96,96,dx::linalg::MatrixLayout::RowMajor,16);
  [unroll]for(uint j=0;j<2;j++){
   uint kr=col_start+j*2;
   B8 kb=B8::Load(aux,(base+kr*16)*96+32,96,dx::linalg::MatrixLayout::ColMajor,16);
   s[j]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,kb);
  }
#else
  A qa=A::Load(qk,((base+qr*16)*64)*2,128,dx::linalg::MatrixLayout::RowMajor,16);
  [unroll]for(uint j=0;j<2;j++){
   uint kr=col_start+j*2;
   B kb=B::Load(qk,((base+kr*16)*64+32)*2,128,dx::linalg::MatrixLayout::ColMajor,16);
   s[j]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,kb);
  }
#endif
  [unroll]for(uint j=0;j<2;j++){
   uint kr=col_start+j*2;
   for(uint i=0;i<s[j].Length();i++){
    uint2 rc=s[j].GetCoordinate(i);uint query=qr*16+rc.x,key=kr*16+rc.y;
#if NATIVE_FAST_ATTENTION
    ex[query*64+key]=float16_t(fast_exp_raw(s[j].Get(i)+W_BIAS(query*64+key)));
#else
    ex[query*64+key]=float16_t(fast_exp(H(s[j].Get(i)+W_BIAS(query*64+key))));
#endif
   }
  }
 }
 GroupMemoryBarrierWithGroupSync();
#if NATIVE_FAST_ATTENTION
 {uint query=t/4,part=t%4;float sum=0;[unroll]for(uint j=0;j<16;j++)sum+=float(ex[query*64+part*16+j]);
  sum+=WaveReadLaneAt(sum,(t&31)^1);sum+=WaveReadLaneAt(sum,(t&31)^2);if(part==0)softmax_inverse[query]=1/sum;}
 GroupMemoryBarrierWithGroupSync();
#if NATIVE_C32_FP8_QKV
 {uint i0=t*16;uint4 w=0;[unroll]for(uint j=0;j<16;j++){uint e=E4M3(Ffast(float(ex[i0+j])*softmax_inverse[i0/64]));w[j/4]|=e<<((j%4)*8);}p8[t*4]=w.x;p8[t*4+1]=w.y;p8[t*4+2]=w.z;p8[t*4+3]=w.w;}
#else
 for(uint i=t;i<4096;i+=256)ex[i]=float16_t(Ffast(float(ex[i])*softmax_inverse[i/64]));
#endif
#else
 if(t<64){
  float parity[2];[unroll]for(uint odd=0;odd<2;odd++){
   float total=0;[unroll]for(uint lane=0;lane<4;lane++){
    uint b0=t*64+odd+(lane%2)*2+(lane/2)*8;float partial=H(float(ex[b0])+float(ex[b0+16]));
    partial=H(partial+H(float(ex[b0+4])+float(ex[b0+20])));partial=H(partial+H(float(ex[b0+32])+float(ex[b0+48])));partial=H(partial+H(float(ex[b0+36])+float(ex[b0+52])));total=lane==0?partial:H(total+partial);
   }parity[odd]=total;
  }
  softmax_inverse[t]=H(1/H(parity[0]+parity[1]));
 }
 GroupMemoryBarrierWithGroupSync();
 for(uint i=t;i<4096;i+=256)ex[i]=float16_t(F(H(float(ex[i])*softmax_inverse[i/64])));
#endif
 GroupMemoryBarrierWithGroupSync();
 {
  uint col=col_start*16;C acc=C::Splat(0.0f);
  [unroll]for(uint g=0;g<2;g++){
#if NATIVE_C32_FP8_QKV
   A8 pa=A8::Load(p8,qr*16*16+g*8,16,dx::linalg::MatrixLayout::RowMajor);
   B8 vb=B8::Load(aux,(base+g*32)*96+64+col,96,dx::linalg::MatrixLayout::RowMajor,16);
#else
   A pa=A::Load(ex,qr*16*64+g*32,64,dx::linalg::MatrixLayout::RowMajor);
   B vb=B::Load(aux,((base+g*32)*32+col)*2,64,dx::linalg::MatrixLayout::RowMajor,16);
#endif
#if NATIVE_FAST_ACCUMULATE
   acc.MultiplyAccumulate(pa,vb);
#else
   C partial=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(pa,vb);
   for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)+partial.Get(i)));
#endif
  }
#if NATIVE_FAST_ACCUMULATE
  for(uint i=0;i<acc.Length();i++)acc.Set(i,F(H(acc.Get(i))));
#else
  for(uint i=0;i<acc.Length();i++)acc.Set(i,F(acc.Get(i)));
#endif
#if NATIVE_C32_FUSED
  // FAST PATH: projection fused. Stage the window's attention output (64x32 f16) in LDS, then
  // each wave projects its 16 queries x 16 output channels and applies the residual.
  acc.Cast<dx::linalg::ComponentType::F16>().Store(attn,qr*16*32+col,32,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
 {
  const uint cr=col_start,first=base+qr*16;
  A aa=A::Load(attn,qr*16*32,32,dx::linalg::MatrixLayout::RowMajor);
  B bb=B::Load(weights,6144+cr*16*32*2,64,dx::linalg::MatrixLayout::ColMajor,16);
  C z=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(aa,bb);
  for(uint i=0;i<z.Length();i++){
   uint2 rc=z.GetCoordinate(i);uint p=first+rc.x,c=cr*16+rc.y;
   float result=H(z.Get(i)+input[p*32+c]*W_RESIDUAL(c));
   z.Set(i,RAW_OUTPUT?result:F(result));
  }
  z.Store(qk,(first*32+cr*16)*4,128,dx::linalg::MatrixLayout::RowMajor,16);
 }
#else
  acc.Cast<dx::linalg::ComponentType::F16>().Store(aux,attn_base()+((base+qr*16)*32+col)*2,64,dx::linalg::MatrixLayout::RowMajor,16);
 }
#endif
}
#else
[WaveSize(32)]
[numthreads(32,1,1)]void projection(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=(gid.x+gid.y*65535u)*16;if(first>=runtime_width*runtime_height)return;
 A aa=A::Load(aux,attn_base()+(first*32)*2,64,dx::linalg::MatrixLayout::RowMajor,16);
 [unroll]for(uint cr=0;cr<2;cr++){
  B bb=B::Load(weights,6144+cr*16*32*2,64,dx::linalg::MatrixLayout::ColMajor,16);
  C z=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(aa,bb);
  for(uint i=0;i<z.Length();i++){
   uint2 rc=z.GetCoordinate(i);uint p=first+rc.x,c=cr*16+rc.y;
#if NATIVE_FAST_ATTENTION
   float result=H(z.Get(i)+input[p*32+c]*W_RESIDUAL(c));
#else
   float result=half_add_preserving_midpoint(z.Get(i),H(input[p*32+c]*W_RESIDUAL(c)));
#endif
   z.Set(i,RAW_OUTPUT?result:F(result));
  }
  z.Store(qk,(first*32+cr*16)*4,128,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
#endif
