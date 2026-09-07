// Multihead window attention without LDS staging of Q/K/V.
// normalize: one thread per (token, head) turns the f32 QKV matrix output into
// the exact values the attention kernel used to compute in its prologue:
//   qn=F(H(H(q*qi)*H(scale[head]))), kn=F(H(k*ki)), vn=F(v), with qi/ki from the
//   original NativeHalfSquarePair tree; stored as f16 (all on the F8/f16 grid)
//   in window-major order [window][token 0..63][3*CHANNELS].
// attention: per (window, head), 256 threads / 8 waves. Q/K/V tiles are wave
// loads straight from that buffer; only the exp table lives in LDS. The
// arithmetic (score bias, exp bit map, denominator tree, F(H(exp*inv)),
// two K32 AV products with H, F(H(a+b))) matches native_c64.hlsl exactly.
#ifndef CHANNELS
#define CHANNELS 64
#endif
#define HEADS (CHANNELS/32)
#define MATRIX (CHANNELS*CHANNELS)
#define SCALE_OFFSET (4*MATRIX+HEADS*4096)
#define STRIDE (3*CHANNELS)
#include <dx/linalg.h>
StructuredBuffer<float> feature:register(t0),weights:register(t1);
ByteAddressBuffer qkv:register(t2);
cbuffer Geometry:register(b0){uint width;uint height;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
#include "native_half_square.hlsli"
uint window_pixel(uint window,uint token){return ((window/(width/8))*8+token/8)*width+(window%(width/8))*8+token%8;}

#if DIRECT_NORMALIZE
RWByteAddressBuffer packed:register(u0);
[numthreads(64,1,1)]void normalize(uint3 id:SV_DispatchThreadID){
 uint n=id.x+id.y*65535u*64u;if(n>=width*height*HEADS)return;
 uint window=n/(64*HEADS),rest=n%(64*HEADS),token=rest/HEADS,head=rest%HEADS,p=window_pixel(window,token);
 float q[32],k[32],qs[16],ks[16];
 [unroll]for(uint c=0;c<32;c++){uint row=head*32+c;q[c]=feature[p*STRIDE+row];k[c]=feature[p*STRIDE+CHANNELS+row];}
 [unroll]for(uint i=0;i<16;i++){qs[i]=NativeHalfSquarePair(q[i],q[i+16]);ks[i]=NativeHalfSquarePair(k[i],k[i+16]);}
 [unroll]for(uint i=0;i<8;i++){qs[i]=H(qs[i*2]+qs[i*2+1]);ks[i]=H(ks[i*2]+ks[i*2+1]);}
 [unroll]for(uint step=4;step>0;step/=2){[loop]for(uint i=0;i<step;i++){qs[i]=H(qs[i]+qs[i+step]);ks[i]=H(ks[i]+ks[i+step]);}}
 float qi=H(rsqrt(max(qs[0],6.198883056640625e-5))),ki=H(rsqrt(max(ks[0],6.198883056640625e-5))),scale=H(weights[SCALE_OFFSET+head]);
 uint base=(window*64+token)*STRIDE+head*32;
 [unroll]for(uint c=0;c<32;c+=2){
  float q0=F(H(H(q[c]*qi)*scale)),q1=F(H(H(q[c+1]*qi)*scale));
  float k0=F(H(k[c]*ki)),k1=F(H(k[c+1]*ki));
  float v0=F(feature[p*STRIDE+2*CHANNELS+head*32+c]),v1=F(feature[p*STRIDE+2*CHANNELS+head*32+c+1]);
  packed.Store((base+c)*2,f32tof16(q0)|(f32tof16(q1)<<16));
  packed.Store((base+CHANNELS+c)*2,f32tof16(k0)|(f32tof16(k1)<<16));
  packed.Store((base+2*CHANNELS+c)*2,f32tof16(v0)|(f32tof16(v1)<<16));
 }
}
#else
RWStructuredBuffer<float> output:register(u0);
groupshared float16_t ex[4096];
groupshared float softmax_inverse[64];
using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
[WaveSize(32)]
[numthreads(256,1,1)]void attention(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint head=gid.y,t=tid.x,window=gid.x;if(window>=width*height/64||head>=HEADS)return;
 uint base=window*64*STRIDE+head*32;
 uint qr=t/64,col_start=(t/32)&1;
 {
  C s[2];
  A qa=A::Load(qkv,(base+qr*16*STRIDE)*2,STRIDE*2,dx::linalg::MatrixLayout::RowMajor,16);
  [unroll]for(uint j=0;j<2;j++){
   uint kr=col_start+j*2;
   B kb=B::Load(qkv,(base+kr*16*STRIDE+CHANNELS)*2,STRIDE*2,dx::linalg::MatrixLayout::ColMajor,16);
   s[j]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,kb);
  }
  [unroll]for(uint j=0;j<2;j++){
   uint kr=col_start+j*2;
   for(uint i=0;i<s[j].Length();i++){
    uint2 rc=s[j].GetCoordinate(i);uint query=qr*16+rc.x,key=kr*16+rc.y;
    float score=H(s[j].Get(i)+weights[4*MATRIX+head*4096+query*64+key]);
    uint bits=f32tof16(clamp(H(score*.044921875+1.30078125),1.03125,1.5693359375));
    ex[query*64+key]=float16_t(f16tof32(((bits<<5)+0x8000u)&65535u));
   }
  }
 }
 GroupMemoryBarrierWithGroupSync();
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
 GroupMemoryBarrierWithGroupSync();
 {
  uint col=col_start*16;
  A pa=A::Load(ex,qr*16*64,64,dx::linalg::MatrixLayout::RowMajor);
  B vb=B::Load(qkv,(base+2*CHANNELS+col)*2,STRIDE*2,dx::linalg::MatrixLayout::RowMajor,16);
  C acc=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(pa,vb);
  for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)));
  pa=A::Load(ex,qr*16*64+32,64,dx::linalg::MatrixLayout::RowMajor);
  vb=B::Load(qkv,(base+32*STRIDE+2*CHANNELS+col)*2,STRIDE*2,dx::linalg::MatrixLayout::RowMajor,16);
  C next=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(pa,vb);
  for(uint i=0;i<acc.Length();i++){
   uint2 rc=acc.GetCoordinate(i);uint query=qr*16+rc.x,pixel=window_pixel(window,query);
   output[pixel*CHANNELS+head*32+col+rc.y]=F(H(acc.Get(i)+next.Get(i)));
  }
 }
}
#endif
