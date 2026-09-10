// FAST PATH (DLSS5_VIT_FUSED_FFN): ViT expand (1024->4096, activation) and contract (4096->1024, residual) in one dispatch.
// One group = 16 tokens, 16 waves (NATIVE_VIT_FUSED_TOK32=0) or 32 tokens, 16 waves, hidden in eighths of 512 channels
// (NATIVE_VIT_FUSED_TOK32=1: every B tile load serves two token tiles, halving the weight traffic per token; partition
// accumulators persist across the two eighths of a K partition, so the K order is unchanged). The hidden layer never leaves the chip: quarter q (hidden channels q*1024..+1024) is
// produced by the 16 waves (64 channels each) into LDS as E4M3, then consumed as contract K partition q (each wave: its own
// 64 output channels). Same operations in the same order as native_wave_vit_blocked.hlsl expand (fp32 MMA accumulation over
// the 32 K steps, Activate = polynomial + one RNE quantization to the FP8 grid, exact E4M3 cast) and its split-K reduce +
// combine (partition accumulators from zero over the K steps in order; final = ((((H(residual*scale)+p0)+p1)+p2)+p3), then
// F(H())), so the result is bit for bit the same; only the hidden layer (E4M3, [token][4096]) and the four f32 partial
// rasters are no longer written and read back. Requires the packed E4M3 tiled input (native_vit_pack8), tiled E4M3 expand
// and contract weights (DLSS5_VIT_TILED), FP8 operands/hidden, fast accumulate/epilogue.
#include <dx/linalg.h>
#ifndef NATIVE_VIT_FUSED_TOK32
#define NATIVE_VIT_FUSED_TOK32 0
#endif
ByteAddressBuffer input8:register(t0);     /* E4M3 [token][1024] as tiles (token/16, k/32) of [16][32] at ((token/16)*32+k/32)*512 */
ByteAddressBuffer eweights:register(t1);   /* expand weights [4096 n][1024 k] E4M3 tiles ((n/16)*32+k/32)*512 of [k 32][j 16] */
StructuredBuffer<float> residual:register(t2);
ByteAddressBuffer cweights:register(t3);   /* contract weights [1024 n][4096 k] E4M3 tiles ((n/16)*128+k/32)*512, then f32 scales at 4096*1024 */
RWByteAddressBuffer output:register(u0);   /* f32 [token][1024] */
cbuffer Geometry:register(b0){uint tokens;}
#define IN_TILE(tok,k) ((((tok)/16)*32+(k)/32)*512+((k)%32))
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
float Ffast(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
float Activate(float v){float g=clamp(v,-4.0,4.0),p=g*(abs(g)*(-.055908203125)+.447265625)+.89453125;return Ffast(v*p);}
using A=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
#if NATIVE_VIT_FUSED_TOK32
#define HID_STRIDE 132 /* uints per token row: 512 E4M3 bytes + 16 bytes of padding */
groupshared uint hid[32*HID_STRIDE];
[WaveSize(32)]
[numthreads(512,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 const uint first=gid.x*32;if(first>=tokens)return;
 const uint w=tid.x/32;             /* wave: expand channels s*512+w*32.. (2 tiles), contract output channels w*64.. (4 tiles) */
 C total[2][4],p[2][4];
 [unroll]for(uint m=0;m<2;m++)[unroll]for(uint n=0;n<4;n++){const uint col=w*64+n*16;total[m][n]=C::Splat(0.0f);for(uint i=0;i<total[m][n].Length();i++){uint2 rc=total[m][n].GetCoordinate(i);uint row=col+rc.y;total[m][n].Set(i,H(residual[(first+m*16+rc.x)*1024+row]*asfloat(cweights.Load(4096*1024+row*4))));}}
 [loop]for(uint s=0;s<8;s++){
  C acc[2][2];[unroll]for(uint m=0;m<2;m++)[unroll]for(uint n=0;n<2;n++)acc[m][n]=C::Splat(0.0f);
  [loop]for(uint g=0;g<32;g++){
   B b[2];[unroll]for(uint n=0;n<2;n++){const uint col=s*512+w*32+n*16;b[n]=B::Load(eweights,((col/16)*32+g)*512,16,dx::linalg::MatrixLayout::RowMajor,16);}
   [unroll]for(uint m=0;m<2;m++){A a=A::Load(input8,IN_TILE(first+m*16,g*32),32,dx::linalg::MatrixLayout::RowMajor,16);[unroll]for(uint n=0;n<2;n++)acc[m][n].MultiplyAccumulate(a,b[n]);}
  }
  if(s)GroupMemoryBarrierWithGroupSync();
  [unroll]for(uint m=0;m<2;m++)[unroll]for(uint n=0;n<2;n++){for(uint i=0;i<acc[m][n].Length();i++)acc[m][n].Set(i,Activate(acc[m][n].Get(i)));acc[m][n].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(hid,m*16*HID_STRIDE+(w*32+n*16)/4,HID_STRIDE,dx::linalg::MatrixLayout::RowMajor);}
  GroupMemoryBarrierWithGroupSync();
  if((s&1)==0){[unroll]for(uint m=0;m<2;m++)[unroll]for(uint n=0;n<4;n++)p[m][n]=C::Splat(0.0f);}
  [loop]for(uint k=0;k<512;k+=32){
   B b[4];[unroll]for(uint n=0;n<4;n++){const uint col=w*64+n*16;b[n]=B::Load(cweights,((col/16)*128+(s*512+k)/32)*512,16,dx::linalg::MatrixLayout::RowMajor,16);}
   [unroll]for(uint m=0;m<2;m++){A a=A::Load(hid,m*16*HID_STRIDE+k/4,HID_STRIDE,dx::linalg::MatrixLayout::RowMajor);[unroll]for(uint n=0;n<4;n++)p[m][n].MultiplyAccumulate(a,b[n]);}
  }
  if(s&1){[unroll]for(uint m=0;m<2;m++)[unroll]for(uint n=0;n<4;n++)for(uint i=0;i<total[m][n].Length();i++)total[m][n].Set(i,total[m][n].Get(i)+p[m][n].Get(i));}
 }
 [unroll]for(uint m=0;m<2;m++)[unroll]for(uint n=0;n<4;n++){for(uint i=0;i<total[m][n].Length();i++)total[m][n].Set(i,F(H(total[m][n].Get(i))));total[m][n].Store(output,((first+m*16)*1024+w*64+n*16)*4,1024*4,dx::linalg::MatrixLayout::RowMajor,16);}
}
#else
#define HID_STRIDE 260 /* uints per token row in LDS: 1024 E4M3 bytes + 16 bytes of padding against bank conflicts */
groupshared uint hid[16*HID_STRIDE];
[WaveSize(32)]
[numthreads(512,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 const uint first=gid.x*16;if(first>=tokens)return;
 const uint w=tid.x/32;             /* wave: expand channels q*1024+w*64.., contract output channels w*64.. */
 C total[4];
 [unroll]for(uint n=0;n<4;n++){const uint col=w*64+n*16;total[n]=C::Splat(0.0f);for(uint i=0;i<total[n].Length();i++){uint2 rc=total[n].GetCoordinate(i);uint row=col+rc.y;total[n].Set(i,H(residual[(first+rc.x)*1024+row]*asfloat(cweights.Load(4096*1024+row*4))));}}
 [loop]for(uint q=0;q<4;q++){
  /* expand: hidden channels q*1024+w*64 .. +64 of the 16 tokens */
  C acc[4];[unroll]for(uint n=0;n<4;n++)acc[n]=C::Splat(0.0f);
  [loop]for(uint g=0;g<32;g++){
   A a=A::Load(input8,IN_TILE(first,g*32),32,dx::linalg::MatrixLayout::RowMajor,16);
   [unroll]for(uint n=0;n<4;n++){const uint col=q*1024+w*64+n*16;B b=B::Load(eweights,((col/16)*32+g)*512,16,dx::linalg::MatrixLayout::RowMajor,16);acc[n].MultiplyAccumulate(a,b);}
  }
  if(q)GroupMemoryBarrierWithGroupSync(); /* every wave has finished reading quarter q-1 */
  [unroll]for(uint n=0;n<4;n++){for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,Activate(acc[n].Get(i)));acc[n].Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(hid,(w*64+n*16)/4,HID_STRIDE,dx::linalg::MatrixLayout::RowMajor);}
  GroupMemoryBarrierWithGroupSync();
  /* contract partition q: K = hidden channels q*1024 .. +1024 (LDS), this wave's 64 output channels */
  C p[4];[unroll]for(uint n=0;n<4;n++)p[n]=C::Splat(0.0f);
  [loop]for(uint k=0;k<1024;k+=32){
   A a=A::Load(hid,k/4,HID_STRIDE,dx::linalg::MatrixLayout::RowMajor);
   [unroll]for(uint n=0;n<4;n++){const uint col=w*64+n*16;B b=B::Load(cweights,((col/16)*128+(q*1024+k)/32)*512,16,dx::linalg::MatrixLayout::RowMajor,16);p[n].MultiplyAccumulate(a,b);}
  }
  [unroll]for(uint n=0;n<4;n++)for(uint i=0;i<total[n].Length();i++)total[n].Set(i,total[n].Get(i)+p[n].Get(i));
 }
 [unroll]for(uint n=0;n<4;n++){for(uint i=0;i<total[n].Length();i++)total[n].Set(i,F(H(total[n].Get(i))));total[n].Store(output,(first*1024+w*64+n*16)*4,1024*4,dx::linalg::MatrixLayout::RowMajor,16);}
}
#endif
