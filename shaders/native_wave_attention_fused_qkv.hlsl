// FAST PATH: multihead window attention with the QKV GEMM + per-token normalize fused into the same dispatch
// (per window x head, 256 threads). Waves 0..3 each produce the E4M3 Q/K/V rows of 16 tokens for this head into LDS;
// all 8 waves then run the register-exp attention (see native_wave_attention_direct.hlsl NATIVE_ATTN_FAST2) from LDS.
// Input: E4M3 residual stream [token][C] in window-major token order. Weights: E4M3 QKV [3C][C] (row-major, or tiled
// 32x16 B blocks when NATIVE_TILED_WEIGHTS). Bias/scale from the f32 attention weight table. Output E4M3 [pixel][C].
#ifndef CHANNELS
#define CHANNELS 64
#endif
#ifndef NATIVE_TILED_WEIGHTS
#define NATIVE_TILED_WEIGHTS 0
#endif
#define HEADS (CHANNELS/32)
#define MATRIX (CHANNELS*CHANNELS)
#define SCALE_OFFSET (4*MATRIX+HEADS*4096)
#include <dx/linalg.h>
ByteAddressBuffer qkvw:register(t0);
StructuredBuffer<float> weights:register(t1);
ByteAddressBuffer input8:register(t2);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;}
float fast_exp_raw(float x){float a=clamp(x*.044921875+1.30078125,1.03125,1.5693359375);uint b=f32tof16(a);return f16tof32(((b<<5)+0x8000u)&65535u);}
uint window_pixel(uint window,uint token){return ((window/(width/8))*8+token/8)*width+(window%(width/8))*8+token%8;}
groupshared uint qkv8[64*24];
groupshared uint atile[2*512];
groupshared float16_t ex[4096];
groupshared uint p8[1024];
groupshared uint otile[512];
groupshared float16_t ones16[512];
groupshared float partial_sum[2*64];
groupshared float inv4[4*32];
groupshared float softmax_inverse[64];
using A8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using AH=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using BH=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
B8 LoadW(uint col,uint g){
#if NATIVE_TILED_WEIGHTS
 return B8::Load(qkvw,((col/16)*(CHANNELS/32)+g)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
#else
 return B8::Load(qkvw,col*CHANNELS+g*32,CHANNELS,dx::linalg::MatrixLayout::ColMajor,16);
#endif
}
[WaveSize(32)]
[numthreads(256,1,1)]void attention(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint head=gid.y,t=tid.x,window=gid.x;if(window>=width*height/64||head>=HEADS)return;
 uint qr=t/64,col_start=(t/32)&1,wave=t/32,lane=t%32,base=window*64;
 for(uint i=t;i<512;i+=256)ones16[i]=float16_t(1.0);
 // All 8 waves run the QKV GEMM group-uniformly (waves 4..7 recompute rows 0..3 and discard them); only LDS writes are gated.
 const bool qwave=wave<4;const uint qfirst=base+(wave&3)*16;
 C z[6];
 [unroll]for(uint k=0;k<6;k++)z[k]=C::Splat(0.0f);
 // Tokens of a window are two 8-pixel raster runs, so each K step gathers the wave's 16 token rows (32 bytes each)
 // into a double-buffered LDS tile (one uint4 per lane) before the A load. Waves 4..7 mirror waves 0..3.
 const uint arow=lane/2,ahalf=lane%2,apixel=window_pixel(window,(wave&3)*16+arow);
 [loop]for(uint g=0;g<CHANNELS/32;g++){
  if(qwave){uint4 v=input8.Load4(apixel*CHANNELS+g*32+ahalf*16);uint o=(g&1)*512+wave*128+arow*8+ahalf*4;atile[o]=v.x;atile[o+1]=v.y;atile[o+2]=v.z;atile[o+3]=v.w;}
  GroupMemoryBarrierWithGroupSync();
  A8 a=A8::Load(atile,(g&1)*512+(wave&3)*128,8,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint part=0;part<3;part++)[unroll]for(uint cr=0;cr<2;cr++){B8 b=LoadW(part*CHANNELS+head*32+cr*16,g);z[part*2+cr].MultiplyAccumulate(a,b);}
 }
 // Row sums of squares (f16 squares, one MMA against all-ones), same as the C32 fast3 kernel.
 [unroll]for(uint h=0;h<2;h++)[unroll]for(uint cr=0;cr<2;cr++){
  C sq=z[h*2+cr];
  for(uint i=0;i<sq.Length();i++){float v=sq.Get(i);sq.Set(i,v*v);}
  if(qwave)sq.Cast<dx::linalg::ComponentType::F16>().Store(ex,wave*1024+h*512+cr*16,32,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
 [unroll]for(uint h=0;h<2;h++){
  AH st=AH::Load(ex,(wave&3)*1024+h*512,32,dx::linalg::MatrixLayout::RowMajor);BH ones=BH::Load(ones16,0,16,dx::linalg::MatrixLayout::RowMajor);
  C rs=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(st,ones);
  if(qwave)for(uint i=0;i<rs.Length();i++){uint2 rc=rs.GetCoordinate(i);if(rc.y==0)inv4[wave*32+h*16+rc.x]=rsqrt(max(rs.Get(i),6.198883056640625e-5))*(h==0?weights[SCALE_OFFSET+head]:1);}
 }
 GroupMemoryBarrierWithGroupSync();
 [unroll]for(uint part=0;part<3;part++)[unroll]for(uint cr=0;cr<2;cr++){
  C tt=z[part*2+cr];
  if(part<2)for(uint i=0;i<tt.Length();i++){uint2 rc=tt.GetCoordinate(i);tt.Set(i,tt.Get(i)*inv4[(wave&3)*32+part*16+rc.x]);}
  if(qwave)tt.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(qkv8,(wave*16)*24+part*8+cr*4,24,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
 // ---- attention (register exp, MMA row sums, hardware E4M3 P) ----
 C s[2];
 {
  A8 qa=A8::Load(qkv8,(qr*16)*24,24,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint j=0;j<2;j++){
   uint kr=col_start+j*2;
   B8 kb=B8::Load(qkv8,(kr*16)*24+8,24,dx::linalg::MatrixLayout::ColMajor);
   s[j]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,kb);
   for(uint i=0;i<s[j].Length();i++){uint2 rc=s[j].GetCoordinate(i);s[j].Set(i,fast_exp_raw(s[j].Get(i)+weights[4*MATRIX+head*4096+(qr*16+rc.x)*64+kr*16+rc.y]));}
   s[j].Cast<dx::linalg::ComponentType::F16>().Store(ex,wave*512+j*16,32,dx::linalg::MatrixLayout::RowMajor);
  }
 }
 GroupMemoryBarrierWithGroupSync();
 {
  AH et=AH::Load(ex,wave*512,32,dx::linalg::MatrixLayout::RowMajor);BH ones=BH::Load(ones16,0,16,dx::linalg::MatrixLayout::RowMajor);
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
  acc.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(otile,wave*64,4,dx::linalg::MatrixLayout::RowMajor);
  GroupMemoryBarrierWithGroupSync();
  [unroll]for(uint k=0;k<2;k++){
   uint slot=lane*2+k,row=slot/4,q=slot%4;
   uint pixel=window_pixel(window,qr*16+row);
   output.Store(pixel*CHANNELS+head*32+col+q*4,otile[wave*64+row*4+q]);
  }
 }
}
