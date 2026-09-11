// FAST PATH (plan step 2a): wave-matrix QKV projection fused with the attention normalize.
// One wave = 16 tokens x 64 output columns = two whole heads of one part (Q, K or V), so the
// per-(token, head) 32-channel square sum is a wave reduction over the accumulators; the
// normalized E4M3-grid values go straight to the window-major f16 buffer the direct attention
// kernel reads. Removes the f32 [pixel][3C] round trip and one dispatch per block.
// Arithmetic = NATIVE_FAST_ATTENTION normalize: qn=Ffast(q*rsqrt(sum q^2)*scale), kn=Ffast(k*rsqrt(sum k^2)), vn=Ffast(v).
#ifndef MATRIX_CHANNELS
#define MATRIX_CHANNELS 64
#endif
#define STRIDE (3*MATRIX_CHANNELS)
#define HEADS (MATRIX_CHANNELS/32)
#define SCALE_OFFSET (4*MATRIX_CHANNELS*MATRIX_CHANNELS+HEADS*4096)
#include <dx/linalg.h>
#include "native_sat_cast.hlsli"
#ifndef NATIVE_INPUT_TILED
#define NATIVE_INPUT_TILED 0
#endif
ByteAddressBuffer input:register(t0),weights:register(t1);
StructuredBuffer<float> attention_weights:register(t2);
RWByteAddressBuffer packed:register(u0);
#ifndef NATIVE_QKV_FAST2
#define NATIVE_QKV_FAST2 0
#endif
#if NATIVE_QKV_FAST2
groupshared float16_t sq16[1024];
groupshared float16_t ones16[512];
using AH=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using BH=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
#else
groupshared float squares[1024];
#endif
groupshared float inv[32];
#ifndef NATIVE_TILED_WEIGHTS
#define NATIVE_TILED_WEIGHTS 0
#endif
#ifndef NATIVE_FP8_QKV_OUT
#define NATIVE_FP8_QKV_OUT 0
#endif
#if NATIVE_FP8_QKV_OUT
groupshared uint tile8[256];
#endif
cbuffer Geometry:register(b0){uint width;uint height;}
float Ffast(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
using A=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID){
 if(gid.x*16>=width*height)return;
 C acc[4];
 [loop]for(uint g=0;g<MATRIX_CHANNELS/32;g++){
#if NATIVE_INPUT_TILED
  /* FAST PATH (split stream8): the projection stored its E4M3 output as 512-byte [token 16][k 32] tiles; no pack dispatch */
  A a=A::Load(input,(gid.x*(MATRIX_CHANNELS/32)+g)*512,32,dx::linalg::MatrixLayout::RowMajor,16);
#else
  A a=A::Load(input,gid.x*16*MATRIX_CHANNELS+g*32,MATRIX_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
#endif
  [unroll]for(uint n=0;n<4;n++){
   uint col=(gid.y*4+n)*16; // 0..3C-1, weights row-major [3C][C] in part order
#if NATIVE_TILED_WEIGHTS
   B b=B::Load(weights,((col/16)*(MATRIX_CHANNELS/32)+g)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
#else
   B b=B::Load(weights,col*MATRIX_CHANNELS+g*32,MATRIX_CHANNELS,dx::linalg::MatrixLayout::ColMajor,16);
#endif
   if(g==0)acc[n]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);else acc[n].MultiplyAccumulate(a,b);
  }
 }
 const uint part=(gid.y*64)/MATRIX_CHANNELS,head0=((gid.y*64)%MATRIX_CHANNELS)/32;
 // Per-token inverse norm for the two heads (tiles 0,1 = head0; tiles 2,3 = head0+1), via LDS:
 // squares scattered by (row, channel), 16 lanes sum their row, epilogue reads inv[head][row].
 const uint lane=WaveGetLaneIndex();
 if(part<2){
#if NATIVE_QKV_FAST2
  // FAST PATH (qkv fast2): squared tiles stored as f16 with matrix Stores, row sums from one MMA against all-ones.
  for(uint i=lane;i<512;i+=32)ones16[i]=float16_t(1.0);
  [unroll]for(uint h=0;h<2;h++)[unroll]for(uint n=0;n<2;n++){C sq=acc[h*2+n];for(uint i=0;i<sq.Length();i++){float v=sq.Get(i);sq.Set(i,v*v);}sq.Cast<dx::linalg::ComponentType::F16>().Store(sq16,h*512+n*16,32,dx::linalg::MatrixLayout::RowMajor);}
  GroupMemoryBarrierWithGroupSync();
  [unroll]for(uint h=0;h<2;h++){
   AH st=AH::Load(sq16,h*512,32,dx::linalg::MatrixLayout::RowMajor);BH ones=BH::Load(ones16,0,16,dx::linalg::MatrixLayout::RowMajor);
   C rs=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(st,ones);
   float scale=part==0?attention_weights[SCALE_OFFSET+head0+h]:1;
   for(uint i=0;i<rs.Length();i++){uint2 rc=rs.GetCoordinate(i);if(rc.y==0)inv[h*16+rc.x]=rsqrt(max(rs.Get(i),6.198883056640625e-5))*scale;}
  }
  GroupMemoryBarrierWithGroupSync();
#else
  [unroll]for(uint h=0;h<2;h++){
   [unroll]for(uint n=0;n<2;n++)for(uint i=0;i<acc[h*2+n].Length();i++){uint2 rc=acc[h*2+n].GetCoordinate(i);float v=acc[h*2+n].Get(i);squares[h*512+rc.x*32+n*16+rc.y]=v*v;}
  }
  GroupMemoryBarrierWithGroupSync();
  {
   const uint h=lane/16,r=lane%16;float ss=0;
   [unroll]for(uint c=0;c<32;c++)ss+=squares[h*512+r*32+c];
   float scale=part==0?attention_weights[SCALE_OFFSET+head0+h]:1;
   inv[h*16+r]=rsqrt(max(ss,6.198883056640625e-5))*scale;
  }
  GroupMemoryBarrierWithGroupSync();
#endif
 }
 [unroll]for(uint n=0;n<4;n++){
  uint row=(gid.y*64)%MATRIX_CHANNELS+n*16;
#if NATIVE_FP8_QKV_OUT
  // Scale rows, hardware-cast to E4M3 into an LDS [16 token][64 byte] tile, then copy out 16-byte chunks.
  if(part<2)for(uint i=0;i<acc[n].Length();i++){uint2 rc=acc[n].GetCoordinate(i);acc[n].Set(i,acc[n].Get(i)*inv[(n/2)*16+rc.x]);}
  SAT8(acc[n]);acc[n].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(tile8,n*4,16,dx::linalg::MatrixLayout::RowMajor);
#else
  for(uint i=0;i<acc[n].Length();i++){
   uint2 rc=acc[n].GetCoordinate(i);
   float k=part<2?inv[(n/2)*16+rc.x]:1;
   uint p=gid.x*16+rc.x,px=p%width,py=p/width,window=(py/8)*(width/8)+px/8,token=(py%8)*8+px%8;
   packed.Store<float16_t>(((window*64+token)*STRIDE+part*MATRIX_CHANNELS+row+rc.y)*2,float16_t(Ffast(acc[n].Get(i)*k)));
  }
#endif
 }
#if NATIVE_FP8_QKV_OUT
 GroupMemoryBarrierWithGroupSync();
 {
  const uint t=lane/2,half=lane%2,p=gid.x*16+t,px=p%width,py=p/width,window=(py/8)*(width/8)+px/8,token=(py%8)*8+px%8;
  const uint dst=(window*64+token)*STRIDE+part*MATRIX_CHANNELS+(gid.y*64)%MATRIX_CHANNELS+half*32,src=t*16+half*8;
  packed.Store4(dst,uint4(tile8[src],tile8[src+1],tile8[src+2],tile8[src+3]));
  packed.Store4(dst+16,uint4(tile8[src+4],tile8[src+5],tile8[src+6],tile8[src+7]));
 }
#endif
}
