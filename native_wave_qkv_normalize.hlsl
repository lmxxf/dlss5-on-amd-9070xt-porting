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
ByteAddressBuffer input:register(t0),weights:register(t1);
StructuredBuffer<float> attention_weights:register(t2);
RWByteAddressBuffer packed:register(u0);
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
  A a=A::Load(input,gid.x*16*MATRIX_CHANNELS+g*32,MATRIX_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
  [unroll]for(uint n=0;n<4;n++){
   uint col=(gid.y*4+n)*16; // 0..3C-1, weights row-major [3C][C] in part order
   B b=B::Load(weights,col*MATRIX_CHANNELS+g*32,MATRIX_CHANNELS,dx::linalg::MatrixLayout::ColMajor,16);
   if(g==0)acc[n]=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);else acc[n].MultiplyAccumulate(a,b);
  }
 }
 const uint part=(gid.y*64)/MATRIX_CHANNELS,head0=((gid.y*64)%MATRIX_CHANNELS)/32;
 // Per-token inverse norm for the two heads (tiles 0,1 = head0; tiles 2,3 = head0+1). V needs none.
 float inv[2][16];
 [unroll]for(uint h=0;h<2;h++){
  float ss[16];[unroll]for(uint r=0;r<16;r++)ss[r]=0;
  if(part<2){
   [unroll]for(uint n=0;n<2;n++)for(uint i=0;i<acc[h*2+n].Length();i++){uint2 rc=acc[h*2+n].GetCoordinate(i);float v=acc[h*2+n].Get(i);[unroll]for(uint r=0;r<16;r++)ss[r]+=(rc.x==r)?v*v:0;}
   float scale=part==0?attention_weights[SCALE_OFFSET+head0+h]:1;
   [unroll]for(uint r=0;r<16;r++)inv[h][r]=rsqrt(max(WaveActiveSum(ss[r]),6.198883056640625e-5))*scale;
  }else{[unroll]for(uint r=0;r<16;r++)inv[h][r]=1;}
 }
 [unroll]for(uint n=0;n<4;n++){
  uint row=(gid.y*64)%MATRIX_CHANNELS+n*16;
  for(uint i=0;i<acc[n].Length();i++){
   uint2 rc=acc[n].GetCoordinate(i);
   float k=1;[unroll]for(uint r=0;r<16;r++)k=(rc.x==r)?inv[n/2][r]:k;
   uint p=gid.x*16+rc.x,px=p%width,py=p/width,window=(py/8)*(width/8)+px/8,token=(py%8)*8+px%8;
   packed.Store<float16_t>(((window*64+token)*STRIDE+part*MATRIX_CHANNELS+row+rc.y)*2,float16_t(Ffast(acc[n].Get(i)*k)));
  }
 }
}
