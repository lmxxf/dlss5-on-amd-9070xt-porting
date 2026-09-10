// Wave-matrix decoder entry / 2x upsample projection, identical numerics to
// native_vit_linear.hlsl DECODER_ENTRY: K partitions (4 when K=1024) with H()
// per K32 step, total=H(p0+p1...), then for each of the 2x2 output positions
// merged=H(total+F(residual)*scale[row]); output = OUT==32 ? merged : F(merged).
#ifndef INPUT_CHANNELS
#define INPUT_CHANNELS 1024
#endif
#ifndef OUTPUT_CHANNELS
#define OUTPUT_CHANNELS 512
#endif
#define BLOCK_N (OUTPUT_CHANNELS>=64?4:2)
#define PARTITIONS (INPUT_CHANNELS==1024?4:1)
#include <dx/linalg.h>
#ifndef SKIP8
#define SKIP8 0
#endif
#ifndef NATIVE_DECODER_TILED
#define NATIVE_DECODER_TILED 0
#endif
#ifndef NATIVE_DECODER_FAST
#define NATIVE_DECODER_FAST 0
#endif
#ifndef NATIVE_DECODER_HW_H
#define NATIVE_DECODER_HW_H 0
#endif
#ifndef NATIVE_DECODER_RESIDUAL_GRID
#define NATIVE_DECODER_RESIDUAL_GRID 0
#endif
/* FAST PATH (DLSS5_BUILD_DECODER_FAST): the epilogue stages each 16x16 tile in LDS and writes the 2x2 upsampled outputs as float4 runs
   (lane order = token, dx, 4-channel quad, so a wave writes contiguous 64-byte pieces), residual/scale read as float4, F() as the bit-level
   RNE quantizer (no log2/exp2). NATIVE_DECODER_HW_H: H() through the hardware f32<->f16 pair. NATIVE_DECODER_RESIDUAL_GRID: the residual
   is already on the E4M3 grid (F(residual)=residual). */
StructuredBuffer<float> input:register(t0);
ByteAddressBuffer weights:register(t1);
#if NATIVE_DECODER_FAST
RWByteAddressBuffer output:register(u0);
groupshared float otile[256];
#else
RWStructuredBuffer<float> output:register(u0);
#endif
#if SKIP8
// FAST PATH (DLSS5_C32_SKIP8): the residual is block 4's main8 (E4M3 bytes, raster over its shifted work grid). F(E4M3)=identity.
ByteAddressBuffer residual8:register(t2);
cbuffer Geometry:register(b0){uint tokens;uint output_base;uint skip_width;uint skip_shift_x;uint skip_shift_y;}
float e4m3_to_float(uint b){uint e=(b>>3)&15u,m=b&7u;float v=e==0?float(m)/512.0:asfloat(((e+120u)<<23)|(m<<20));return (b&0x80u)?-v:v;}
float skip_at(uint x,uint y,uint c){uint a=((y+skip_shift_y)*skip_width+x+skip_shift_x)*32+c;return e4m3_to_float((residual8.Load(a&~3u)>>((a&3u)*8))&255u);}
#else
#if NATIVE_DECODER_FAST
ByteAddressBuffer residual:register(t2);
#else
StructuredBuffer<float> residual:register(t2);
#endif
cbuffer Geometry:register(b0){uint tokens;uint output_base;}
#endif
groupshared float16_t tile[512];
#if NATIVE_DECODER_HW_H
float H(float v){return f16tof32(f32tof16(v));}
#else
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
#endif
#if NATIVE_DECODER_FAST
float F(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
#else
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
#endif
using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16;if(first>=tokens)return;
 C total[BLOCK_N];
 [loop]for(uint part=0;part<PARTITIONS;part++){
  C acc[BLOCK_N];[unroll]for(uint n=0;n<BLOCK_N;n++)acc[n]=C::Splat(0.0f);
  [loop]for(uint k=part*(INPUT_CHANNELS/PARTITIONS);k<(part+1)*(INPUT_CHANNELS/PARTITIONS);k+=32){
   for(uint i=tid.x;i<512;i+=32)tile[i]=float16_t(input[(first+i/32)*INPUT_CHANNELS+k+i%32]);
   GroupMemoryBarrierWithGroupSync();
   A a=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
   [unroll]for(uint n=0;n<BLOCK_N;n++){
    uint col=(gid.y*BLOCK_N+n)*16;
#if NATIVE_DECODER_TILED
    /* FAST PATH (DLSS5_DECODER_TILED): f16 weight tiles (col/16, k/32) of [k 32][j 16] at ((col/16)*(K/32)+k/32)*1024 (no power-of-two row strides). */
    B b=B::Load(weights,((col/16)*(INPUT_CHANNELS/32)+k/32)*1024,32,dx::linalg::MatrixLayout::RowMajor,16);
#else
    B b=B::Load(weights,(col*INPUT_CHANNELS+k)*2,INPUT_CHANNELS*2,dx::linalg::MatrixLayout::ColMajor,16);
#endif
#if NATIVE_FAST_ACCUMULATE
    acc[n].MultiplyAccumulate(a,b);
#else
    C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
    for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,H(acc[n].Get(i)+p.Get(i)));
#endif
   }
   GroupMemoryBarrierWithGroupSync();
  }
#if NATIVE_FAST_ACCUMULATE
  [unroll]for(uint n=0;n<BLOCK_N;n++){if(part==0)total[n]=acc[n];else for(uint i=0;i<total[n].Length();i++)total[n].Set(i,total[n].Get(i)+acc[n].Get(i));}
#else
  [unroll]for(uint n=0;n<BLOCK_N;n++){if(part==0)total[n]=acc[n];else for(uint i=0;i<total[n].Length();i++)total[n].Set(i,H(total[n].Get(i)+acc[n].Get(i)));}
#endif
 }
#if NATIVE_FAST_ACCUMULATE
 [unroll]for(uint n=0;n<BLOCK_N;n++)for(uint i=0;i<total[n].Length();i++)total[n].Set(i,H(total[n].Get(i)));
#endif
 uint width=tokens==138240?480:tokens==34560?240:tokens==8640?120:tokens==2160?60:tokens==640?32:tokens==16384?128:tokens==4096?64:tokens==1024?32:tokens==256?16:8;
 uint output_width=tokens==640?60:width*2,output_height=tokens==138240?576:tokens==34560?288:tokens==8640?144:tokens==2160?72:tokens==640?36:width*2;
#if NATIVE_DECODER_FAST
 [unroll]for(uint n=0;n<BLOCK_N;n++){
  const uint col=(gid.y*BLOCK_N+n)*16;
  total[n].Store(otile,0,16,dx::linalg::MatrixLayout::RowMajor);
  GroupMemoryBarrierWithGroupSync();
  [unroll]for(uint s=0;s<8;s++){
   const uint slot=s*32+tid.x,dy=slot/128,r=(slot%128)/8,dx=(slot%8)/4,q=slot%4;
   const uint token=first+r;if(token>=tokens)continue;
   const uint x=token%width*2+dx,y=token/width*2+dy;
   if(x>=output_width||y>=output_height)continue;
   const uint index=(y*output_width+x)*OUTPUT_CHANNELS+col+q*4;
   const float4 t4=float4(otile[r*16+q*4],otile[r*16+q*4+1],otile[r*16+q*4+2],otile[r*16+q*4+3]);
   const float4 scale=asfloat(weights.Load4(INPUT_CHANNELS*OUTPUT_CHANNELS*2+(col+q*4)*4));
#if SKIP8
   const uint a=((y+skip_shift_y)*skip_width+x+skip_shift_x)*32+col+q*4;const uint sk4=residual8.Load(a);
   const float4 res=float4(e4m3_to_float(sk4&255u),e4m3_to_float((sk4>>8)&255u),e4m3_to_float((sk4>>16)&255u),e4m3_to_float(sk4>>24));
#elif NATIVE_DECODER_RESIDUAL_GRID
   const float4 res=asfloat(residual.Load4(index*4));
#else
   const float4 raw=asfloat(residual.Load4(index*4));const float4 res=float4(F(raw.x),F(raw.y),F(raw.z),F(raw.w));
#endif
   float4 merged;
#if SKIP8
   [unroll]for(uint c=0;c<4;c++){precise float prod=res[c]*scale[c];precise float sum=t4[c]+prod;merged[c]=H(sum);}
#else
   [unroll]for(uint c=0;c<4;c++)merged[c]=H(t4[c]+res[c]*scale[c]);
#endif
   if(OUTPUT_CHANNELS!=32){[unroll]for(uint c=0;c<4;c++)merged[c]=F(merged[c]);}
   output.Store4(index*4,asuint(merged));
  }
  GroupMemoryBarrierWithGroupSync();
 }
#else
 [unroll]for(uint n=0;n<BLOCK_N;n++){
  uint col=(gid.y*BLOCK_N+n)*16;
  for(uint i=0;i<total[n].Length();i++){
   uint2 rc=total[n].GetCoordinate(i);uint token=first+rc.x,row=col+rc.y;float t=total[n].Get(i);
   float scale=asfloat(weights.Load(INPUT_CHANNELS*OUTPUT_CHANNELS*2+row*4));
   [unroll]for(uint dy=0;dy<2;dy++)[unroll]for(uint dx=0;dx<2;dx++){
    uint x=token%width*2+dx,y=token/width*2+dy;
    if(x>=output_width||y>=output_height)continue;
    uint index=(y*output_width+x)*OUTPUT_CHANNELS+row;
#if SKIP8
    /* precise: the f32 chain must not be contracted into an FMA, or 18% of the outputs move by one f16 ulp against the crop path */
    precise float prod=skip_at(x,y,row)*scale;precise float sum=t+prod;float merged=H(sum);
#else
    float merged=H(t+F(residual[index])*scale);
#endif
    output[index]=OUTPUT_CHANNELS==32?merged:F(merged);
   }
  }
 }
#endif
}
