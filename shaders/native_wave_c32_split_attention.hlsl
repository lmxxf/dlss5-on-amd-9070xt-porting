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
#include "native_sat_cast.hlsli"
ByteAddressBuffer weights:register(t0);
#ifndef NATIVE_C32_ATTN_FAST2
#define NATIVE_C32_ATTN_FAST2 0
#endif
#ifndef NATIVE_C32_HALF_STREAM
#define NATIVE_C32_HALF_STREAM 0
#endif
#if NATIVE_C32_ATTN_FAST2
ByteAddressBuffer input:register(t1);
#if NATIVE_C32_HALF_STREAM
// FAST PATH (DLSS5_C32_HALF_STREAM): the FFN output (t1) and the block output (u0) are f16; every value on them is H()-rounded, so this is exact.
#define INPUT_AT(i) float(input.Load<float16_t>((i)*2))
using C16=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
#define LOAD_IN(first) C16::Load(input,(first)*64,64,dx::linalg::MatrixLayout::RowMajor,16).Cast<dx::linalg::ComponentType::F32>()
#define LOAD_IN1(first) C16::Load(input,(first)*64+32,64,dx::linalg::MatrixLayout::RowMajor,16).Cast<dx::linalg::ComponentType::F32>()
#define STORE_OUT(z,first,cr) z.Cast<dx::linalg::ComponentType::F16>().Store(qk,((first)*32+(cr)*16)*2,64,dx::linalg::MatrixLayout::RowMajor,16)
#else
#define INPUT_AT(i) asfloat(input.Load((i)*4))
#define LOAD_IN(first) C::Load(input,(first)*128,128,dx::linalg::MatrixLayout::RowMajor,16)
#define LOAD_IN1(first) C::Load(input,(first)*128+64,128,dx::linalg::MatrixLayout::RowMajor,16)
#define STORE_OUT(z,first,cr) z.Store(qk,((first)*32+(cr)*16)*4,128,dx::linalg::MatrixLayout::RowMajor,16)
#endif
#else
StructuredBuffer<float> input:register(t1);
#define INPUT_AT(i) input[i]
#endif
RWByteAddressBuffer qk:register(u0);
RWByteAddressBuffer aux:register(u1);
#ifndef NATIVE_C32_EPILOGUE
#define NATIVE_C32_EPILOGUE 0
#endif
#ifndef NATIVE_C32_TWO_PASS_SOFTMAX
#define NATIVE_C32_TWO_PASS_SOFTMAX 0
#endif
#ifndef NATIVE_C32_SAT_CAST
#define NATIVE_C32_SAT_CAST 0 /* saturate before hardware E4M3 casts (see native_c32_ffn_fused.hlsli) */
#endif
#ifndef NATIVE_C32_EPILOGUE_EXACT_HEAD
#define NATIVE_C32_EPILOGUE_EXACT_HEAD 0
#endif
#if NATIVE_C32_EPILOGUE
// FAST PATH (DLSS5_C32_EPILOGUE): the finish stage (main8 + 2x2 pooled down) or the post70 rgb head runs in the attention epilogue
// on the block output while it is still in registers. epilogue_mode: 0 raw only; 1 raw + main8 + down; 2 main8 + down (no raw);
// 3 rgb head (no raw). A wave's 16 tokens are two full rows of one 8x8 window, so the 2x2 pooling never leaves the wave.
#ifndef NATIVE_C32_FUSED_FFN
#define NATIVE_C32_FUSED_FFN 0
#endif
#if NATIVE_C32_FUSED_FFN
/* FAST PATH (DLSS5_C32_FUSED_FFN): + the FFN input mapping of native_wave_c32_ffn_blocked.hlsl (19 root constants) */
cbuffer RuntimeGeometry:register(b0){uint runtime_seed;uint runtime_width;uint runtime_height;uint local_oracle;uint temporal_enabled;uint epilogue_mode;uint rgb_scale_bits;uint rgb_width;uint rgb_height;uint rgb_shift_x;uint rgb_shift_y;
 uint map_mode;uint src_width;uint src_height;uint shift_x;uint shift_y;uint prev_shift_x;uint prev_shift_y;uint prev_work_width;}
#else
cbuffer RuntimeGeometry:register(b0){uint runtime_seed;uint runtime_width;uint runtime_height;uint local_oracle;uint temporal_enabled;uint epilogue_mode;uint rgb_scale_bits;uint rgb_width;uint rgb_height;uint rgb_shift_x;uint rgb_shift_y;}
#endif
StructuredBuffer<float> color:register(t2);      /* rgb head: HWC color [pixel][4] */
StructuredBuffer<float> head_w:register(t3);     /* rgb head: 3 x 32 weights */
RWByteAddressBuffer main8_out:register(u2);      /* E4M3 main, raster over the work grid */
RWStructuredBuffer<float> down_out:register(u3); /* 2x2 pooled, raster over the half work grid */
RWStructuredBuffer<float> rgb_out:register(u4);  /* rgb head output [pixel][3] */
/* preblock_finish.hlsl's F (round half away from zero on the mantissa); the fast F above rounds to even, and the two differ on ties. */
/* preblock_finish.hlsl's H and F, bit for bit (the fast H above is the hardware f32tof16). */
float Hfinish(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float Ffinish(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
float aligned_half(int sum,float acc,float scale,int e){
 float scaled_acc=acc*scale;
 if(scaled_acc!=trunc(scaled_acc)||abs(scaled_acc)>=67108864.0)return asfloat(0x7fc00000u);
 bool negative=sum<0,aneg=acc<0;uint magnitude=negative?(0u-asuint(sum)):asuint(sum),other=(uint)abs(scaled_acc);
 if(negative==aneg)magnitude+=other;
 else if(magnitude>=other)magnitude-=other;
 else{magnitude=other-magnitude;negative=aneg;}
 if(magnitude==0)return 0;
 int quantum_exp=e-27,top=firstbithigh(magnitude),step_exp=max(top+quantum_exp,-14)-10,drop=step_exp-quantum_exp;
 uint rounded=magnitude;
 if(drop>32)rounded=0;
 else if(drop==32)rounded=magnitude>0x80000000u?1:0;
 else if(drop>0){rounded=magnitude>>drop;uint remainder=magnitude&((1u<<drop)-1u),half=1u<<(drop-1);if(remainder>half||(remainder==half&&(rounded&1u)))rounded++;}
 else step_exp=quantum_exp;
 float value=float(rounded)*exp2(float(step_exp));if(value>=65520.0)value=asfloat(0x7f800000u);
 return negative?-value:value;
}
#else
cbuffer RuntimeGeometry:register(b0){uint runtime_seed;uint runtime_width;uint runtime_height;uint local_oracle;uint temporal_enabled;}
#endif
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
#ifndef NATIVE_C32_ATTN_FAST2
#define NATIVE_C32_ATTN_FAST2 0
#endif
#if NATIVE_C32_ATTN_FAST2
#if !(NATIVE_C32_FUSED&&NATIVE_C32_FP8_QKV)
#error NATIVE_C32_ATTN_FAST2 needs the fused FP8 QKV
#endif
// FAST PATH (qkv fast2): input tile loaded as accumulators and cast to E4M3 for an FP8 x FP8 QKV GEMM (E4M3 weight copy
// at byte 25856); row sums of squares from one MMA of the f16 squared tile against all-ones; no scalar F()/LDS reductions.
groupshared uint in8[128];
groupshared float16_t sq16[2*512];
groupshared float16_t ones16[512];
using A8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
[WaveSize(32)]
[numthreads(32,1,1)]void qkv(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=(gid.x+gid.y*65535u)*16;if(first>=runtime_width*runtime_height)return;
 for(uint i=tid.x;i<512;i+=32)ones16[i]=float16_t(1.0);
 {C in0=C::Load(input,first*128,128,dx::linalg::MatrixLayout::RowMajor,16),in1=C::Load(input,first*128+64,128,dx::linalg::MatrixLayout::RowMajor,16);
  SAT8(in0);SAT8(in1);in0.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(in8,0,8,dx::linalg::MatrixLayout::RowMajor);
  in1.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(in8,4,8,dx::linalg::MatrixLayout::RowMajor);}
 GroupMemoryBarrierWithGroupSync();
 A8 a=A8::Load(in8,0,8,dx::linalg::MatrixLayout::RowMajor);
 C z[6];
 [unroll]for(uint part=0;part<3;part++)[unroll]for(uint cr=0;cr<2;cr++){
  B8 b=B8::Load(weights,25856+(part*32+cr*16)*32,32,dx::linalg::MatrixLayout::ColMajor,16);
  z[part*2+cr]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
 }
 [unroll]for(uint h=0;h<2;h++)[unroll]for(uint cr=0;cr<2;cr++){C sq=z[h*2+cr];for(uint i=0;i<sq.Length();i++){float v=sq.Get(i);sq.Set(i,v*v);}sq.Cast<dx::linalg::ComponentType::F16>().Store(sq16,h*512+cr*16,32,dx::linalg::MatrixLayout::RowMajor);}
 GroupMemoryBarrierWithGroupSync();
 [unroll]for(uint h=0;h<2;h++){
  A st=A::Load(sq16,h*512,32,dx::linalg::MatrixLayout::RowMajor);B ones=B::Load(ones16,0,16,dx::linalg::MatrixLayout::RowMajor);
  C rs=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(st,ones);
  for(uint i=0;i<rs.Length();i++){uint2 rc=rs.GetCoordinate(i);if(rc.y==0)inv[h*16+rc.x]=rsqrt(max(rs.Get(i),6.198883056640625e-5))*(h==0?W_SCALE:1);}
 }
 GroupMemoryBarrierWithGroupSync();
 [unroll]for(uint part=0;part<3;part++)[unroll]for(uint cr=0;cr<2;cr++){
  C t=z[part*2+cr];
  if(part<2)for(uint i=0;i<t.Length();i++){uint2 rc=t.GetCoordinate(i);t.Set(i,t.Get(i)*inv[part*16+rc.x]);}
  SAT8(t);t.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(aux,first*96+part*32+cr*16,96,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
#else
[WaveSize(32)]
[numthreads(32,1,1)]void qkv(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=(gid.x+gid.y*65535u)*16;if(first>=runtime_width*runtime_height)return;
 for(uint i=tid.x;i<512;i+=32)tile[i]=float16_t(F(INPUT_AT(first*32+i)));
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
  SAT8(t);t.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(aux,first*96+part*32+cr*16,96,dx::linalg::MatrixLayout::RowMajor,16);
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
#endif
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
#ifndef NATIVE_C32_ATTN_FAST2
#define NATIVE_C32_ATTN_FAST2 0
#endif
#if NATIVE_C32_ATTN_FAST2
#if !(NATIVE_C32_FUSED&&NATIVE_C32_FP8_QKV&&NATIVE_FAST_ATTENTION&&NATIVE_FAST_ACCUMULATE)
#error NATIVE_C32_ATTN_FAST2 needs the fused FP8 fast attention
#endif
#ifndef NATIVE_C32_ATTN_FAST3
#define NATIVE_C32_ATTN_FAST3 0
#endif
#ifndef NATIVE_C32_ATTN_FAST4
#define NATIVE_C32_ATTN_FAST4 0
#endif
#ifndef NATIVE_C32_BIAS_TILE
#define NATIVE_C32_BIAS_TILE 0
#endif
#ifndef NATIVE_C32_OUT8
#define NATIVE_C32_OUT8 0
#endif
#if NATIVE_C32_ATTN_FAST4
#if !NATIVE_C32_ATTN_FAST3
#error NATIVE_C32_ATTN_FAST4 needs the fused QKV of fast3
#endif
// FAST PATH (attention fast4): one group = two 8x8 windows (128 tokens). All 8 waves produce the E4M3 Q/K/V rows of their
// own 16 tokens into LDS; then each wave runs its 16 queries against all 64 keys of its own window, so the softmax row
// sums never leave the wave (no partial_sum, no group sync after the Q/K/V store). Scratch is wave-private: ex holds the
// squares and then the exp tiles (1KB per wave), pw8 holds the input tile, then P, then the E4M3 attention output (1KB per
// wave). Two group syncs in total: ones16 ready, Q/K/V ready. Numerics as fast3 (row sum accumulated over two K32 MMAs).
groupshared uint qkv8[128*24];
groupshared uint pw8[8*256];
groupshared float16_t ones16[512];
#if NATIVE_C32_FUSED_FFN
#if !(NATIVE_C32_EPILOGUE&&NATIVE_C32_HALF_STREAM)
#error NATIVE_C32_FUSED_FFN needs the epilogue constants and the half-stream C16 loads
#endif
#include "native_c32_ffn_fused.hlsli"
#endif
[WaveSize(32)]
[numthreads(256,1,1)]void attention(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 const uint t=tid.x,wave=t/32,window=gid.x*2+wave/4,windows=runtime_width*runtime_height/64;
 const bool live=window<windows;const uint qfirst=window*64+(wave&3)*16,lfirst=wave*16,lwin=(wave/4)*64,pbase=wave*256,ebase=wave*512;
 for(uint i=t;i<512;i+=256)ones16[i]=float16_t(1.0);
 C z[6];
#if NATIVE_C32_FUSED_FFN
 C ffn_out[2]; /* the FFN output tile: QKV input and projection residual, never leaves the registers */
#endif
 if(live){
#if NATIVE_C32_FUSED_FFN
  ffn_fused(qfirst,t&31,ebase,pbase,lfirst*24,ffn_out[0],ffn_out[1]);
#if NATIVE_C32_FUSED_FFN_PROBE
  /* test build: the FFN output goes to aux (= raw) in the ffn-buffer layout for a CPU comparison with the standalone FFN; 1 = skip the attention, 2 = continue (a stage whose epilogue writes no raw keeps it) */
  ffn_out[0].Cast<dx::linalg::ComponentType::F16>().Store(aux,(qfirst*32)*2,64,dx::linalg::MatrixLayout::RowMajor,16);ffn_out[1].Cast<dx::linalg::ComponentType::F16>().Store(aux,(qfirst*32+16)*2,64,dx::linalg::MatrixLayout::RowMajor,16);
#if NATIVE_C32_FUSED_FFN_PROBE==1
 }
 return;
 if(live){
#endif
#endif
  C in0=ffn_out[0],in1=ffn_out[1];
#if NATIVE_C32_SAT_CAST
  for(uint si=0;si<in0.Length();si++){in0.Set(si,clamp(in0.Get(si),-448.0,448.0));in1.Set(si,clamp(in1.Get(si),-448.0,448.0));}
#endif
#else
  C in0=LOAD_IN(qfirst),in1=LOAD_IN1(qfirst);
#endif
  SAT8(in0);SAT8(in1);in0.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(pw8,pbase,8,dx::linalg::MatrixLayout::RowMajor);
  in1.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(pw8,pbase+4,8,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
 if(live){
  A8 a=A8::Load(pw8,pbase,8,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint part=0;part<3;part++)[unroll]for(uint cr=0;cr<2;cr++){
   B8 b=B8::Load(weights,25856+(part*32+cr*16)*32,32,dx::linalg::MatrixLayout::ColMajor,16);
   z[part*2+cr]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
  }
  B ones=B::Load(ones16,0,16,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint h=0;h<2;h++){
   [unroll]for(uint cr=0;cr<2;cr++){C sq=z[h*2+cr];for(uint i=0;i<sq.Length();i++){float v=sq.Get(i);sq.Set(i,v*v);}sq.Cast<dx::linalg::ComponentType::F16>().Store(ex,ebase+cr*16,32,dx::linalg::MatrixLayout::RowMajor);}
   GroupMemoryBarrier();
   A st=A::Load(ex,ebase,32,dx::linalg::MatrixLayout::RowMajor);
   C rs=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(st,ones);
   [unroll]for(uint cr=0;cr<2;cr++){for(uint i=0;i<rs.Length();i++)z[h*2+cr].Set(i,z[h*2+cr].Get(i)*(rsqrt(max(rs.Get(i),6.198883056640625e-5))*(h==0?W_SCALE:1)));}
   GroupMemoryBarrier();
  }
  [unroll]for(uint part=0;part<3;part++)[unroll]for(uint cr=0;cr<2;cr++){SAT8(z[part*2+cr]);z[part*2+cr].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(qkv8,lfirst*24+part*8+cr*4,24,dx::linalg::MatrixLayout::RowMajor);}
 }
 GroupMemoryBarrierWithGroupSync();
 if(!live)return;
#if NATIVE_C32_TWO_PASS_SOFTMAX
 /* FAST PATH (DLSS5_BUILD_C32_TWO_PASS): the softmax in two passes so that only one 16x16 score accumulator is live at a time instead
    of four. Pass 1: QK -> exp -> f16 into LDS -> row sums (MMA against ones). Pass 2: QK again (4 small MMAs, the K tiles are still in
    LDS) -> exp -> x 1/sum -> E4M3 P tile. The exp values are recomputed bit for bit, so the output is identical; the four live s[]
    accumulators (32 VGPRs) were what pushed the fused kernel to 128 VGPRs and 896 bytes of scratch (DevHistory 09-10 18:00). */
 C rs=C::Splat(0.0f);
 {
  A8 qa=A8::Load(qkv8,lfirst*24,24,dx::linalg::MatrixLayout::RowMajor);
  B ones=B::Load(ones16,0,16,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint g=0;g<2;g++){
   [unroll]for(uint j=0;j<2;j++){const uint kr=g*2+j;
    B8 kb=B8::Load(qkv8,(lwin+kr*16)*24+8,24,dx::linalg::MatrixLayout::ColMajor);
    C sc=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,kb);
    for(uint i=0;i<sc.Length();i++){uint2 rc=sc.GetCoordinate(i);sc.Set(i,fast_exp_raw(sc.Get(i)+W_BIAS(((wave&3)*16+rc.x)*64+kr*16+rc.y)));}
    sc.Cast<dx::linalg::ComponentType::F16>().Store(ex,ebase+j*16,32,dx::linalg::MatrixLayout::RowMajor);
   }
   GroupMemoryBarrier();
   A et=A::Load(ex,ebase,32,dx::linalg::MatrixLayout::RowMajor);
   rs.MultiplyAccumulate(et,ones);
   GroupMemoryBarrier();
  }
  [unroll]for(uint kr=0;kr<4;kr++){
   B8 kb=B8::Load(qkv8,(lwin+kr*16)*24+8,24,dx::linalg::MatrixLayout::ColMajor);
   C sc=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,kb);
   for(uint i=0;i<sc.Length();i++){uint2 rc=sc.GetCoordinate(i);sc.Set(i,fast_exp_raw(sc.Get(i)+W_BIAS(((wave&3)*16+rc.x)*64+kr*16+rc.y))*(1/rs.Get(i)));}
   sc.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(pw8,pbase+kr*4,16,dx::linalg::MatrixLayout::RowMajor);
  }
 }
#else
 C s[4];
 {
  A8 qa=A8::Load(qkv8,lfirst*24,24,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint kr=0;kr<4;kr++){
   B8 kb=B8::Load(qkv8,(lwin+kr*16)*24+8,24,dx::linalg::MatrixLayout::ColMajor);
   s[kr]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,kb);
#if NATIVE_C32_BIAS_TILE
   /* FAST PATH (DLSS5_BUILD_C32_BIAS_TILE): the 16x16 block of the [64][64] f32 bias table as one accumulator-layout load instead of
      32 per-element scalar loads with 64-bit address arithmetic (those held ~100 VGPRs in flight and made the kernel spill). Same values. */
   {const C bias=C::Load(weights,8192+(((wave&3)*16)*64+kr*16)*4,256,dx::linalg::MatrixLayout::RowMajor,16);for(uint i=0;i<s[kr].Length();i++)s[kr].Set(i,fast_exp_raw(s[kr].Get(i)+bias.Get(i)));}
#else
   for(uint i=0;i<s[kr].Length();i++){uint2 rc=s[kr].GetCoordinate(i);s[kr].Set(i,fast_exp_raw(s[kr].Get(i)+W_BIAS(((wave&3)*16+rc.x)*64+kr*16+rc.y)));}
#endif
  }
 }
 C rs=C::Splat(0.0f);
 {
  B ones=B::Load(ones16,0,16,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint g=0;g<2;g++){
   s[g*2].Cast<dx::linalg::ComponentType::F16>().Store(ex,ebase,32,dx::linalg::MatrixLayout::RowMajor);
   s[g*2+1].Cast<dx::linalg::ComponentType::F16>().Store(ex,ebase+16,32,dx::linalg::MatrixLayout::RowMajor);
   GroupMemoryBarrier();
   A et=A::Load(ex,ebase,32,dx::linalg::MatrixLayout::RowMajor);
   rs.MultiplyAccumulate(et,ones);
   GroupMemoryBarrier();
  }
 }
 [unroll]for(uint kr=0;kr<4;kr++){
  for(uint i=0;i<s[kr].Length();i++)s[kr].Set(i,s[kr].Get(i)*(1/rs.Get(i)));
  s[kr].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(pw8,pbase+kr*4,16,dx::linalg::MatrixLayout::RowMajor);
 }
#endif
 GroupMemoryBarrier();
 C acc[2];acc[0]=C::Splat(0.0f);acc[1]=C::Splat(0.0f);
 [unroll]for(uint g=0;g<2;g++){
  A8 pa=A8::Load(pw8,pbase+g*8,16,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint cc=0;cc<2;cc++){
   B8 vb=B8::Load(qkv8,(lwin+g*32)*24+16+cc*4,24,dx::linalg::MatrixLayout::RowMajor);
   acc[cc].MultiplyAccumulate(pa,vb);
  }
 }
 GroupMemoryBarrier();
#if NATIVE_C32_SAT_CAST
 [unroll]for(uint cc=0;cc<2;cc++)for(uint si=0;si<acc[cc].Length();si++)acc[cc].Set(si,clamp(acc[cc].Get(si),-448.0,448.0)); /* AV output can exceed 448 (P rounds to slightly above 1) */
#endif
 [unroll]for(uint cc=0;cc<2;cc++)acc[cc].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(pw8,pbase+cc*4,8,dx::linalg::MatrixLayout::RowMajor);
 GroupMemoryBarrier();
 {
  A8 aa=A8::Load(pw8,pbase,8,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint cr=0;cr<2;cr++){
   B8 bb=B8::Load(weights,24832+cr*16*32,32,dx::linalg::MatrixLayout::ColMajor,16);
   C zp=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(aa,bb);
   for(uint i=0;i<zp.Length();i++){
    uint2 rc=zp.GetCoordinate(i);uint p=qfirst+rc.x,c=cr*16+rc.y;
#if NATIVE_C32_FUSED_FFN
    precise float prod=ffn_out[cr].Get(i)*W_RESIDUAL(c);precise float sum=zp.Get(i)+prod;float result=H(sum); /* same accumulator layout as zp: element i is (p, c); precise as below */
#elif NATIVE_C32_PRECISE_CHAIN
    precise float prod=INPUT_AT(p*32+c)*W_RESIDUAL(c);precise float sum=zp.Get(i)+prod;float result=H(sum); /* no FMA contraction: matches the fused-FFN kernel */
#else
    float result=H(zp.Get(i)+INPUT_AT(p*32+c)*W_RESIDUAL(c));
#endif
    zp.Set(i,RAW_OUTPUT?result:F(result));
   }
#if NATIVE_C32_EPILOGUE
   if(epilogue_mode<=1||epilogue_mode==4)STORE_OUT(zp,qfirst,cr); /* 4 = rgb head + raw (test) */
   if(epilogue_mode>=1)zp.Cast<dx::linalg::ComponentType::F16>().Store(ex,ebase+cr*16,32,dx::linalg::MatrixLayout::RowMajor); /* raw tile [16 tokens][32], exact (H-rounded) */
   if(epilogue_mode>=1&&epilogue_mode<=2){C q=zp;for(uint i=0;i<q.Length();i++)q.Set(i,F(q.Get(i)));q.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(pw8,pbase+cr*4,8,dx::linalg::MatrixLayout::RowMajor);} /* main8 tile [16][32] bytes */
#else
   STORE_OUT(zp,qfirst,cr);
#endif
#if NATIVE_C32_OUT8
   // FAST PATH (post70 out8): E4M3 copy of the block output into aux ([token][32] bytes) for the rgb head (71MB instead of 283MB).
   zp.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(aux,qfirst*32+cr*16,32,dx::linalg::MatrixLayout::RowMajor,16);
#endif
  }
#if NATIVE_C32_EPILOGUE
  if(epilogue_mode>=1){
   GroupMemoryBarrier(); /* the wave's own ex tile */
   /* token -> raster over the work grid (shift already folded into the grid): tile-major, 16 tokens = rows r0, r0+1 of one tile */
   const uint tiles_x=runtime_width/8,tile=qfirst/64,tx=(tile%tiles_x)*8,ty=(tile/tiles_x)*8+((qfirst%64)/8);
   const uint lane=t&31;
   if(epilogue_mode<=2){
    /* main8: E4M3(F(raw)) through the matrix cast into pw8 (free after the projection); lane l copies 16 bytes = half of token l/2 */
    {const uint tok=lane>>1,half=lane&1;
     const uint x=tx+(tok&7),y=ty+(tok>>3);main8_out.Store4(((y*runtime_width+x)*32+half*16),uint4(pw8[pbase+tok*8+half*4],pw8[pbase+tok*8+half*4+1],pw8[pbase+tok*8+half*4+2],pw8[pbase+tok*8+half*4+3]));}
    /* down: 2x2 pool of the raw values, F(H(H(top+bottom)*.25)); lane = channel, 4 pooled pixels per wave */
    {const uint c=lane;[unroll]for(uint k=0;k<4;k++){
      float a00=float(ex[ebase+(2*k)*32+c]),a10=float(ex[ebase+(2*k+1)*32+c]),a01=float(ex[ebase+(8+2*k)*32+c]),a11=float(ex[ebase+(8+2*k+1)*32+c]);
      float top=H(a00+a10),bottom=H(a01+a11);
      down_out[(((ty/2)*(runtime_width/2))+(tx/2)+k)*32+c]=F(H(H(top+bottom)*.25));}}
   }else if(epilogue_mode>=3){
    /* rgb head (exact integer-alignment emulation of native_post70.hlsl finish): 16 tokens x 3 rows = 48 tasks over 32 lanes */
    const float input_scale=asfloat(rgb_scale_bits);
    [unroll(2)]for(uint task=lane;task<48;task+=32){
     const uint tok=task/3,row=task%3,wx=tx+(tok&7),wy=ty+(tok>>3);if(wx<rgb_shift_x||wy<rgb_shift_y)continue;const uint x=wx-rgb_shift_x,y=wy-rgb_shift_y;if(x>=rgb_width||y>=rgb_height)continue;const uint pix=y*rgb_width+x;float acc=0; /* work grid -> output raster (the crop) */
#if NATIVE_C32_EPILOGUE_EXACT_HEAD
     [unroll]for(uint part=0;part<2;part++){
      float products[16];int e=acc==0?-1000:int((asuint(acc)>>23)&255u)-125;
      [unroll]for(uint j=0;j<16;j++){float a=float(ex[ebase+tok*32+part*16+j]),b=head_w[row*32+part*16+j];products[j]=a*b;if(products[j]!=0)e=max(e,int((asuint(a)>>23)&255u)+int((asuint(b)>>23)&255u)-252);}
      if(e!=-1000){float scale=asfloat(uint(27-e+127)<<23);int sum=0;
       [unroll]for(uint j=0;j<16;j++)sum+=(int)(products[j]*scale);
       acc=aligned_half(sum,acc,scale,e);
      }
     }
#else
     /* native_post70.hlsl finish_fast: plain f32 dot product, one f16 rounding (PSNR-equivalent; the exact integer-alignment emulation costs 0.5ms here) */
     [unroll]for(uint j=0;j<32;j++)acc+=float(ex[ebase+tok*32+j])*head_w[row*32+j];
     acc=f16tof32(f32tof16(acc));
#endif
     precise float base=color[pix*4+row]*0.125-0.0625;
     precise float encoded=acc*input_scale+base;
     precise float rgb=encoded*8.0+0.5;
     /* test probes (DLSS5_TEST_EPILOGUE_MODE): 5 feature, 6 color, 7 head weight, 8 acc */
     rgb_out[pix*3+row]=epilogue_mode==5?float(ex[ebase+tok*32+row*10]):epilogue_mode==6?color[pix*4+row]:epilogue_mode==7?head_w[row*32+(tok&31)]:epilogue_mode==8?acc:clamp(rgb,0.0,1.0);
    }
   }
  }
#endif
 }
}
#elif NATIVE_C32_ATTN_FAST3
// FAST PATH (attention fast3): the QKV GEMM + normalize runs inside the attention dispatch. Waves 0..3 each produce the
// E4M3 Q/K/V rows of 16 tokens into LDS ([token][96] bytes, same layout as aux); all 8 waves then run the fast2
// attention from LDS. Saves the aux round trip and one dispatch. Reuses ex (squares) and p8 (input tile) as scratch.
groupshared uint qkv8[64*24];
groupshared float inv4[4*32];
groupshared float partial_sum[2*64];
groupshared uint attn8[64*8];
groupshared float16_t ones16[512];
[WaveSize(32)]
[numthreads(256,1,1)]void attention(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint window=gid.x,t=tid.x;if(window*64>=runtime_width*runtime_height)return;
 uint qr=t/64,col_start=(t/32)&1,base=window*64,wave=t/32,lane=t%32;
 for(uint i=t;i<512;i+=256)ones16[i]=float16_t(1.0);
 const bool qwave=wave<4;const uint qfirst=base+wave*16;
 if(qwave){C in0=C::Load(input,qfirst*128,128,dx::linalg::MatrixLayout::RowMajor,16),in1=C::Load(input,qfirst*128+64,128,dx::linalg::MatrixLayout::RowMajor,16);
  SAT8(in0);SAT8(in1);in0.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(p8,wave*128,8,dx::linalg::MatrixLayout::RowMajor);
  in1.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(p8,wave*128+4,8,dx::linalg::MatrixLayout::RowMajor);}
 GroupMemoryBarrierWithGroupSync();
 C z[6];
 if(qwave){
  A8 a=A8::Load(p8,wave*128,8,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint part=0;part<3;part++)[unroll]for(uint cr=0;cr<2;cr++){
   B8 b=B8::Load(weights,25856+(part*32+cr*16)*32,32,dx::linalg::MatrixLayout::ColMajor,16);
   z[part*2+cr]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
  }
  [unroll]for(uint h=0;h<2;h++)[unroll]for(uint cr=0;cr<2;cr++){C sq=z[h*2+cr];for(uint i=0;i<sq.Length();i++){float v=sq.Get(i);sq.Set(i,v*v);}sq.Cast<dx::linalg::ComponentType::F16>().Store(ex,wave*1024+h*512+cr*16,32,dx::linalg::MatrixLayout::RowMajor);}
 }
 GroupMemoryBarrierWithGroupSync();
 if(qwave){
  [unroll]for(uint h=0;h<2;h++){
   A st=A::Load(ex,wave*1024+h*512,32,dx::linalg::MatrixLayout::RowMajor);B ones=B::Load(ones16,0,16,dx::linalg::MatrixLayout::RowMajor);
   C rs=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(st,ones);
   for(uint i=0;i<rs.Length();i++){uint2 rc=rs.GetCoordinate(i);if(rc.y==0)inv4[wave*32+h*16+rc.x]=rsqrt(max(rs.Get(i),6.198883056640625e-5))*(h==0?W_SCALE:1);}
  }
 }
 GroupMemoryBarrierWithGroupSync();
 if(qwave){
  [unroll]for(uint part=0;part<3;part++)[unroll]for(uint cr=0;cr<2;cr++){
   C tt=z[part*2+cr];
   if(part<2)for(uint i=0;i<tt.Length();i++){uint2 rc=tt.GetCoordinate(i);tt.Set(i,tt.Get(i)*inv4[wave*32+part*16+rc.x]);}
   tt.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(qkv8,(wave*16)*24+part*8+cr*4,24,dx::linalg::MatrixLayout::RowMajor);
  }
 }
 GroupMemoryBarrierWithGroupSync();
 C s[2];
 {
  A8 qa=A8::Load(qkv8,(qr*16)*24,24,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint j=0;j<2;j++){
   uint kr=col_start+j*2;
   B8 kb=B8::Load(qkv8,(kr*16)*24+8,24,dx::linalg::MatrixLayout::ColMajor);
   s[j]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,kb);
   for(uint i=0;i<s[j].Length();i++){uint2 rc=s[j].GetCoordinate(i);s[j].Set(i,fast_exp_raw(s[j].Get(i)+W_BIAS((qr*16+rc.x)*64+kr*16+rc.y)));}
   s[j].Cast<dx::linalg::ComponentType::F16>().Store(ex,wave*512+j*16,32,dx::linalg::MatrixLayout::RowMajor);
  }
 }
 GroupMemoryBarrierWithGroupSync();
 {
  A et=A::Load(ex,wave*512,32,dx::linalg::MatrixLayout::RowMajor);
  B ones=B::Load(ones16,0,16,dx::linalg::MatrixLayout::RowMajor);
  C rs=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(et,ones);
  for(uint i=0;i<rs.Length();i++){uint2 rc=rs.GetCoordinate(i);if(rc.y==0)partial_sum[col_start*64+qr*16+rc.x]=rs.Get(i);}
 }
 GroupMemoryBarrierWithGroupSync();
 if(t<64)softmax_inverse[t]=1/(partial_sum[t]+partial_sum[64+t]);
 GroupMemoryBarrierWithGroupSync();
 [unroll]for(uint j=0;j<2;j++){
  uint kr=col_start+j*2;
  for(uint i=0;i<s[j].Length();i++){uint2 rc=s[j].GetCoordinate(i);s[j].Set(i,s[j].Get(i)*softmax_inverse[qr*16+rc.x]);}
  s[j].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(p8,qr*16*16+kr*4,16,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
 {
  uint col=col_start*16;C acc=C::Splat(0.0f);
  [unroll]for(uint g=0;g<2;g++){
   A8 pa=A8::Load(p8,qr*16*16+g*8,16,dx::linalg::MatrixLayout::RowMajor);
   B8 vb=B8::Load(qkv8,(g*32)*24+16+col/4,24,dx::linalg::MatrixLayout::RowMajor);
   acc.MultiplyAccumulate(pa,vb);
  }
  SAT8(acc);acc.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(attn8,qr*16*8+col/4,8,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
 {
  const uint cr=col_start,first=base+qr*16;
  A8 aa=A8::Load(attn8,qr*16*8,8,dx::linalg::MatrixLayout::RowMajor);
  B8 bb=B8::Load(weights,24832+cr*16*32,32,dx::linalg::MatrixLayout::ColMajor,16);
  C z=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(aa,bb);
  for(uint i=0;i<z.Length();i++){
   uint2 rc=z.GetCoordinate(i);uint p=first+rc.x,c=cr*16+rc.y;
   float result=H(z.Get(i)+INPUT_AT(p*32+c)*W_RESIDUAL(c));
   z.Set(i,RAW_OUTPUT?result:F(result));
  }
  z.Store(qk,(first*32+cr*16)*4,128,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
#else
// FAST PATH (attention fast2): scores stay in registers through exp; each wave stores its 16x32 f16 exp tile with one
// matrix Store, row sums come from one MMA against an all-ones B, P is quantized by the hardware E4M3 cast, the PV output
// is cast to E4M3 for an FP8 x FP8 projection (E4M3 projection weight copy at byte 24832). No scalar Ffast/E4M3 loops.
groupshared float partial_sum[2*64];
groupshared uint attn8[64*8];
groupshared float16_t ones16[512];
[WaveSize(32)]
[numthreads(256,1,1)]void attention(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint window=gid.x,t=tid.x;if(window*64>=runtime_width*runtime_height)return;
 uint qr=t/64,col_start=(t/32)&1,base=window*64,wave=t/32;
 for(uint i=t;i<512;i+=256)ones16[i]=float16_t(1.0);
 C s[2];
 {
  A8 qa=A8::Load(aux,(base+qr*16)*96,96,dx::linalg::MatrixLayout::RowMajor,16);
  [unroll]for(uint j=0;j<2;j++){
   uint kr=col_start+j*2;
   B8 kb=B8::Load(aux,(base+kr*16)*96+32,96,dx::linalg::MatrixLayout::ColMajor,16);
   s[j]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,kb);
   for(uint i=0;i<s[j].Length();i++){uint2 rc=s[j].GetCoordinate(i);s[j].Set(i,fast_exp_raw(s[j].Get(i)+W_BIAS((qr*16+rc.x)*64+kr*16+rc.y)));}
   // exp values are f16-exact (built from f16 bits), so the f16 tile equals the legacy ex[] contents.
   s[j].Cast<dx::linalg::ComponentType::F16>().Store(ex,wave*512+j*16,32,dx::linalg::MatrixLayout::RowMajor);
  }
 }
 GroupMemoryBarrierWithGroupSync();
 {
  A et=A::Load(ex,wave*512,32,dx::linalg::MatrixLayout::RowMajor);
  B ones=B::Load(ones16,0,16,dx::linalg::MatrixLayout::RowMajor);
  C rs=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(et,ones);
  for(uint i=0;i<rs.Length();i++){uint2 rc=rs.GetCoordinate(i);if(rc.y==0)partial_sum[col_start*64+qr*16+rc.x]=rs.Get(i);}
 }
 GroupMemoryBarrierWithGroupSync();
 if(t<64)softmax_inverse[t]=1/(partial_sum[t]+partial_sum[64+t]);
 GroupMemoryBarrierWithGroupSync();
 [unroll]for(uint j=0;j<2;j++){
  uint kr=col_start+j*2;
  for(uint i=0;i<s[j].Length();i++){uint2 rc=s[j].GetCoordinate(i);s[j].Set(i,s[j].Get(i)*softmax_inverse[qr*16+rc.x]);}
  s[j].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(p8,qr*16*16+kr*4,16,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
 {
  uint col=col_start*16;C acc=C::Splat(0.0f);
  [unroll]for(uint g=0;g<2;g++){
   A8 pa=A8::Load(p8,qr*16*16+g*8,16,dx::linalg::MatrixLayout::RowMajor);
   B8 vb=B8::Load(aux,(base+g*32)*96+64+col,96,dx::linalg::MatrixLayout::RowMajor,16);
   acc.MultiplyAccumulate(pa,vb);
  }
  SAT8(acc);acc.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(attn8,qr*16*8+col/4,8,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
 {
  const uint cr=col_start,first=base+qr*16;
  A8 aa=A8::Load(attn8,qr*16*8,8,dx::linalg::MatrixLayout::RowMajor);
  B8 bb=B8::Load(weights,24832+cr*16*32,32,dx::linalg::MatrixLayout::ColMajor,16);
  C z=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(aa,bb);
  for(uint i=0;i<z.Length();i++){
   uint2 rc=z.GetCoordinate(i);uint p=first+rc.x,c=cr*16+rc.y;
   float result=H(z.Get(i)+INPUT_AT(p*32+c)*W_RESIDUAL(c));
   z.Set(i,RAW_OUTPUT?result:F(result));
  }
  z.Store(qk,(first*32+cr*16)*4,128,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
#endif
#else
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
   float result=H(z.Get(i)+INPUT_AT(p*32+c)*W_RESIDUAL(c));
   z.Set(i,RAW_OUTPUT?result:F(result));
  }
  z.Store(qk,(first*32+cr*16)*4,128,dx::linalg::MatrixLayout::RowMajor,16);
 }
#else
  acc.Cast<dx::linalg::ComponentType::F16>().Store(aux,attn_base()+((base+qr*16)*32+col)*2,64,dx::linalg::MatrixLayout::RowMajor,16);
 }
#endif
}
#endif
#elif PASS==4
// FAST PATH: one wave per window, no group synchronisation. Requires NATIVE_C32_FP8_QKV layout (aux [token][96] E4M3),
// fast attention arithmetic, and the fused projection semantics. Each of the four 16-query batches is independent:
// QK (4 MMA) -> exp in registers -> row sums by wave reductions -> P as E4M3 through a 1KB wave-private LDS tile ->
// AV (4 MMA) -> F(H) -> f16 tile in LDS -> projection (2 MMA) -> residual -> store.
using A8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
groupshared uint p8[256];
groupshared float16_t attn[16*32];
[WaveSize(32)]
[numthreads(32,1,1)]void attention_wave(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint window=gid.x+gid.y*65535u,t=tid.x;if(window*64>=runtime_width*runtime_height)return;
 const uint base=window*64;
 B8 kb[4];[unroll]for(uint kr=0;kr<4;kr++)kb[kr]=B8::Load(aux,(base+kr*16)*96+32,96,dx::linalg::MatrixLayout::ColMajor,16);
 [loop]for(uint qr=0;qr<4;qr++){
  A8 qa=A8::Load(aux,(base+qr*16)*96,96,dx::linalg::MatrixLayout::RowMajor,16);
  C s[4];float rowsum[16];[unroll]for(uint r=0;r<16;r++)rowsum[r]=0;
  [unroll]for(uint kr=0;kr<4;kr++){
   s[kr]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,kb[kr]);
   for(uint i=0;i<s[kr].Length();i++){uint2 rc=s[kr].GetCoordinate(i);float e=fast_exp_raw(s[kr].Get(i)+W_BIAS((qr*16+rc.x)*64+kr*16+rc.y));s[kr].Set(i,e);[unroll]for(uint r=0;r<16;r++)rowsum[r]+=(rc.x==r)?e:0;}
  }
  float inv[16];[unroll]for(uint r=0;r<16;r++)inv[r]=1/WaveActiveSum(rowsum[r]);
  [unroll]for(uint kr=0;kr<4;kr++){
   for(uint i=0;i<s[kr].Length();i++){uint2 rc=s[kr].GetCoordinate(i);float k=0;[unroll]for(uint r=0;r<16;r++)k=(rc.x==r)?inv[r]:k;s[kr].Set(i,s[kr].Get(i)*k);}
   s[kr].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(p8,kr*4,16,dx::linalg::MatrixLayout::RowMajor);
  }
  GroupMemoryBarrierWithGroupSync();
  C acc[2];[unroll]for(uint col=0;col<2;col++){acc[col]=C::Splat(0.0f);
   [unroll]for(uint g=0;g<2;g++){A8 pa=A8::Load(p8,g*8,16,dx::linalg::MatrixLayout::RowMajor);B8 vb=B8::Load(aux,(base+g*32)*96+64+col*16,96,dx::linalg::MatrixLayout::RowMajor,16);acc[col].MultiplyAccumulate(pa,vb);}
   for(uint i=0;i<acc[col].Length();i++)acc[col].Set(i,F(H(acc[col].Get(i))));
   acc[col].Cast<dx::linalg::ComponentType::F16>().Store(attn,col*16,32,dx::linalg::MatrixLayout::RowMajor);
  }
  GroupMemoryBarrierWithGroupSync();
  {
   A aa=A::Load(attn,0,32,dx::linalg::MatrixLayout::RowMajor);const uint first=base+qr*16;
   [unroll]for(uint cr=0;cr<2;cr++){
    B bb=B::Load(weights,6144+cr*16*32*2,64,dx::linalg::MatrixLayout::ColMajor,16);
    C z=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(aa,bb);
    for(uint i=0;i<z.Length();i++){uint2 rc=z.GetCoordinate(i);uint p=first+rc.x,c=cr*16+rc.y;float result=H(z.Get(i)+INPUT_AT(p*32+c)*W_RESIDUAL(c));z.Set(i,RAW_OUTPUT?result:F(result));}
    z.Store(qk,(first*32+cr*16)*4,128,dx::linalg::MatrixLayout::RowMajor,16);
   }
  }
  GroupMemoryBarrierWithGroupSync();
 }
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
   float result=H(z.Get(i)+INPUT_AT(p*32+c)*W_RESIDUAL(c));
#else
   float result=half_add_preserving_midpoint(z.Get(i),H(INPUT_AT(p*32+c)*W_RESIDUAL(c)));
#endif
   z.Set(i,RAW_OUTPUT?result:F(result));
  }
  z.Store(qk,(first*32+cr*16)*4,128,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
#endif
