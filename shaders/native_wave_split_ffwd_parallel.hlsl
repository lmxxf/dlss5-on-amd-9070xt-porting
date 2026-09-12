#include <dx/linalg.h>
// Dispatch Y selects one of eight independent 64-channel FFN groups.
// Compute only that group's mix rows; all 512 input channels remain dependencies.
// The original full-group fused candidate remains in native_wave_split_ffwd.hlsl.
#ifndef NATIVE_SPLIT_MAPPED
#define NATIVE_SPLIT_MAPPED 0
#endif
#ifndef NATIVE_SPLIT_IN8
#define NATIVE_SPLIT_IN8 0
#endif
#if NATIVE_SPLIT_IN8
/* FAST PATH (DLSS5_SPLIT_STREAM8): the block input is the previous block's E4M3 raster ([pixel][512] bytes) */
ByteAddressBuffer input:register(t0);
#else
StructuredBuffer<float> input:register(t0);
#endif
ByteAddressBuffer weights:register(t1);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;uint raster_width;uint raster_height;uint pad_x;uint pad_y;}
#if NATIVE_SPLIT_MAPPED
/* FAST PATH (DLSS5_SPLIT_STREAM8): no window pack dispatch; padded work token -> raster element index (or -1 on the border, staged as zero) */
int raster_index(uint p){int x=int(p%width)-int(pad_x),y=int(p/width)-int(pad_y);if(x<0||y<0||x>=int(raster_width)||y>=int(raster_height))return -1;return int((uint(y)*raster_width+uint(x))*512);}
#if NATIVE_SPLIT_IN8
float SourceAt(int src,uint c){uint a=uint(src)+c;uint b=(input.Load(a&~3u)>>((a&3u)*8u))&255u;uint e=(b>>3)&15u,m=b&7u;float v=e?asfloat(((e+120u)<<23)|(m<<20)):float(m)*0.001953125;return (b&0x80u)?-v:v;}
#else
float SourceAt(int src,uint c){return input[uint(src)+c];}
#endif
#endif
#ifndef NATIVE_SPLIT_FFWD_FP8
#define NATIVE_SPLIT_FFWD_FP8 0
#endif
#if !NATIVE_SPLIT_FFWD_FP8
groupshared float16_t mixed[16*80],hidden[16*272],tile[512];
groupshared float temp[256];
#endif
#ifndef NATIVE_HW_H
#define NATIVE_HW_H 0
#endif
#if NATIVE_HW_H
float H(float v){return f16tof32(f32tof16(v));}
float F(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
#else
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
#endif
#if NATIVE_FAST_EPILOGUE
// FAST PATH stage 3a: activation polynomial without intermediate f16 roundings; single RNE quantization to the FP8 grid.
float Ffast(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
float Activate(float v){float g=clamp(v,-4.0,4.0),p=g*(abs(g)*(-.055908203125)+.447265625)+.89453125;return Ffast(v*p);}
#endif
#ifndef NATIVE_SPLIT_FFWD_TILED
#define NATIVE_SPLIT_FFWD_TILED 0
#endif
#ifndef NATIVE_SPLIT_FFWD8
#define NATIVE_SPLIT_FFWD8 0
#endif
/* FAST PATH (DLSS5_SPLIT_FFWD_TILED): the three f16 weight matrices are stored as contiguous 1KB [k 32][j 16] tiles
   (tile index = (n/16)*(K/32)+k/32, packed by the host); no 1024/256/512-byte row strides. Bit-exact.
   FAST PATH (DLSS5_SPLIT_FFWD8): the output is already F(H()) i.e. on the E4M3 grid, so it is stored as E4M3 bytes
   in 512-byte [token 16][k 32] tiles and the projection reads its A tiles directly (NATIVE_FP8_INPUT_TILED) instead of re-quantizing f32. Bit-exact. */
#ifndef NATIVE_SPLIT_FFWD_WAVES4
#define NATIVE_SPLIT_FFWD_WAVES4 0
#endif
#if NATIVE_SPLIT_FFWD_FP8
/* FAST PATH (DLSS5_SPLIT_FFWD_FP8): the waves4 kernel with the three matrices on the FP8 wave-matrix path. Every operand already sits
   on the E4M3 grid (block input = previous block's E4M3 raster, mix/expand outputs = F(H()) / Activate(), weights checked E4M3-exact
   by the host), so the products are the same; only the accumulation order inside the hardware instruction may differ. Weights are
   512-byte [k 32][j 16] E4M3 tiles (tile index = (n/16)*(K/32)+k/32); LDS holds E4M3 bytes (row strides 32 / 80 / 272 bytes). */
#if !(NATIVE_SPLIT_FFWD_WAVES4&&NATIVE_SPLIT_FFWD_TILED&&NATIVE_SPLIT_FFWD8&&NATIVE_SPLIT_MAPPED&&NATIVE_SPLIT_IN8&&NATIVE_FAST_EPILOGUE)
#error NATIVE_SPLIT_FFWD_FP8 needs the waves4 + tiled + ffwd8 + mapped + in8 fast path
#endif
groupshared uint tile8[128],mixed8[16*20],hidden8[16*68];
[WaveSize(32)]
[numthreads(128,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16,t=tid.x;if(first>=width*height)return;
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 const uint group=gid.y,wave=t/32;
 {
  C acc=C::Splat(0.0f);
  /* one uint (4 channels) of one token per thread and K step: token t/8, channels k*32+(t%8)*4 .. +3 */
  const int row=raster_index(first+t/8);
  for(uint k=0;k<16;k++){
   tile8[t]=row<0?0u:input.Load(uint(row)+k*32+(t%8)*4);
   GroupMemoryBarrierWithGroupSync();
   A a=A::Load(tile8,0,8,dx::linalg::MatrixLayout::RowMajor);
   B b=B::Load(weights,((group*4+wave)*16+k)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
   acc.MultiplyAccumulate(a,b);
   GroupMemoryBarrierWithGroupSync();
  }
  for(uint i=0;i<acc.Length();i++)acc.Set(i,F(H(acc.Get(i))));
  acc.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(mixed8,wave*4,20,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
 {
  [unroll]for(uint j=0;j<4;j++){
   const uint block=wave*4+j;
   C acc=C::Splat(0.0f);
   [unroll]for(uint k=0;k<2;k++){
    A a=A::Load(mixed8,k*8,20,dx::linalg::MatrixLayout::RowMajor);
    B b=B::Load(weights,262144+group*16384+(block*2+k)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
    acc.MultiplyAccumulate(a,b);
   }
   for(uint i=0;i<acc.Length();i++)acc.Set(i,Activate(H(acc.Get(i))));
   acc.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(hidden8,block*4,68,dx::linalg::MatrixLayout::RowMajor);
  }
 }
 GroupMemoryBarrierWithGroupSync();
 {
  C acc=C::Splat(0.0f);
  for(uint k=0;k<8;k++){
   A a=A::Load(hidden8,k*8,68,dx::linalg::MatrixLayout::RowMajor);
   B b=B::Load(weights,393216+group*16384+(wave*8+k)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
   acc.MultiplyAccumulate(a,b);
  }
  for(uint i=0;i<acc.Length();i++)acc.Set(i,F(H(acc.Get(i))));
  acc.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(output,((first/16)*16+(group*64+wave*16)/32)*512+((group*64+wave*16)%32),32,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
#elif NATIVE_SPLIT_FFWD_WAVES4
// FAST PATH: four waves per 16-token group. mix: one 16-column block per wave (all 16 K steps, input tile
// staged once per K step by the whole group); expand: four hidden blocks per wave; contract: one output block per wave.
// Same arithmetic as the blocked fast path; only the work split changes (2160-token layers are latency-bound).
[WaveSize(32)]
[numthreads(128,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16,t=tid.x;if(first>=width*height)return;
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 const uint group=gid.y,wave=t/32;
 {
  C acc=C::Splat(0.0f);
#if NATIVE_SPLIT_MAPPED
  int rows[4];[unroll]for(uint j=0;j<4;j++)rows[j]=raster_index(first+(t+j*128)/32);
#endif
  for(uint k=0;k<16;k++){
#if NATIVE_SPLIT_MAPPED
   [unroll]for(uint j=0;j<4;j++){const uint i=t+j*128;const int src=rows[j];tile[i]=float16_t(src<0?0.0:SourceAt(src,k*32+i%32));}
#else
   for(uint i=t;i<512;i+=128)tile[i]=float16_t(input[(first+i/32)*512+k*32+i%32]);
#endif
   GroupMemoryBarrierWithGroupSync();
   A a=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
#if NATIVE_SPLIT_FFWD_TILED
   B b=B::Load(weights,((group*4+wave)*16+k)*1024,32,dx::linalg::MatrixLayout::RowMajor,16);
#else
   B b=B::Load(weights,((group*64+wave*16)*512+k*32)*2,1024,dx::linalg::MatrixLayout::ColMajor,16);
#endif
   acc.MultiplyAccumulate(a,b);
   GroupMemoryBarrierWithGroupSync();
  }
  for(uint i=0;i<acc.Length();i++)acc.Set(i,F(H(acc.Get(i))));
  acc.Cast<dx::linalg::ComponentType::F16>().Store(mixed,wave*16,80,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
 {
  [unroll]for(uint j=0;j<4;j++){
   const uint block=wave*4+j;
   C acc=C::Splat(0.0f);
   [unroll]for(uint k=0;k<2;k++){
    A a=A::Load(mixed,k*32,80,dx::linalg::MatrixLayout::RowMajor);
#if NATIVE_SPLIT_FFWD_TILED
    B b=B::Load(weights,262144*2+group*16384*2+(block*2+k)*1024,32,dx::linalg::MatrixLayout::RowMajor,16);
#else
    B b=B::Load(weights,(262144+group*16384+block*16*64+k*32)*2,128,dx::linalg::MatrixLayout::ColMajor,16);
#endif
    acc.MultiplyAccumulate(a,b);
   }
   for(uint i=0;i<acc.Length();i++)acc.Set(i,Activate(H(acc.Get(i))));
   acc.Cast<dx::linalg::ComponentType::F16>().Store(hidden,block*16,272,dx::linalg::MatrixLayout::RowMajor);
  }
 }
 GroupMemoryBarrierWithGroupSync();
 {
  C acc=C::Splat(0.0f);
  for(uint k=0;k<8;k++){
   A a=A::Load(hidden,k*32,272,dx::linalg::MatrixLayout::RowMajor);
#if NATIVE_SPLIT_FFWD_TILED
   B b=B::Load(weights,393216*2+group*16384*2+(wave*8+k)*1024,32,dx::linalg::MatrixLayout::RowMajor,16);
#else
   B b=B::Load(weights,(393216+group*16384+wave*16*256+k*32)*2,512,dx::linalg::MatrixLayout::ColMajor,16);
#endif
   acc.MultiplyAccumulate(a,b);
  }
  for(uint i=0;i<acc.Length();i++)acc.Set(i,F(H(acc.Get(i))));
#if NATIVE_SPLIT_FFWD8
  /* 512-byte [k 32][16 tokens] tiles: tile (first/16)*16 + col/32, this wave's 16 columns are the low or high half of each 32-byte row */
  acc.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(output,((first/16)*16+(group*64+wave*16)/32)*512+((group*64+wave*16)%32),32,dx::linalg::MatrixLayout::RowMajor,16);
#else
  acc.Store(output,(first*512+group*64+wave*16)*4,512*4,dx::linalg::MatrixLayout::RowMajor,16);
#endif
 }
}
#else
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16,t=tid.x;if(first>=width*height)return;
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 uint group=gid.y;
 #if NATIVE_SPLIT_FFWD_BLOCKED
 // Register-blocked: the input tile is staged once per K step for all four mix blocks.
 {
  C acc[4];[unroll]for(uint block=0;block<4;block++)acc[block]=C::Splat(0.0f);
  for(uint k=0;k<16;k++){
   for(uint i=t;i<512;i+=32)tile[i]=float16_t(input[(first+i/32)*512+k*32+i%32]);
   GroupMemoryBarrierWithGroupSync();
   A a=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
   [unroll]for(uint block=0;block<4;block++){
    B b=B::Load(weights,((group*64+block*16)*512+k*32)*2,1024,dx::linalg::MatrixLayout::ColMajor,16);
#if NATIVE_FAST_ACCUMULATE
    acc[block].MultiplyAccumulate(a,b);
#else
    C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
    for(uint i=0;i<acc[block].Length();i++)acc[block].Set(i,H(acc[block].Get(i)+p.Get(i)));
#endif
   }
   GroupMemoryBarrierWithGroupSync();
  }
  [unroll]for(uint block=0;block<4;block++){
#if NATIVE_FAST_ACCUMULATE
   for(uint i=0;i<acc[block].Length();i++){uint2 rc=acc[block].GetCoordinate(i);mixed[rc.x*80+block*16+rc.y]=float16_t(F(H(acc[block].Get(i))));}
#else
   for(uint i=0;i<acc[block].Length();i++){uint2 rc=acc[block].GetCoordinate(i);mixed[rc.x*80+block*16+rc.y]=float16_t(F(acc[block].Get(i)));}
#endif
  }
  GroupMemoryBarrierWithGroupSync();
 }
 #else
 for(uint block=0;block<4;block++){
  C acc=C::Splat(0.0f);
  for(uint k=0;k<16;k++){
   for(uint i=t;i<512;i+=32)tile[i]=float16_t(input[(first+i/32)*512+k*32+i%32]);
   GroupMemoryBarrierWithGroupSync();
   A a=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
   B b=B::Load(weights,((group*64+block*16)*512+k*32)*2,1024,dx::linalg::MatrixLayout::ColMajor,16);
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)+p.Get(i)));
   GroupMemoryBarrierWithGroupSync();
  }
  for(uint i=0;i<acc.Length();i++)acc.Set(i,F(acc.Get(i)));
  acc.Store(temp,0,16,dx::linalg::MatrixLayout::RowMajor);GroupMemoryBarrierWithGroupSync();
  for(uint i=t;i<256;i+=32)mixed[(i/16)*80+block*16+i%16]=float16_t(temp[i]);
  GroupMemoryBarrierWithGroupSync();
 }
 #endif
 {
  for(uint block=0;block<16;block++){
   C acc=C::Splat(0.0f);
   for(uint k=0;k<2;k++){
    A a=A::Load(mixed,k*32,80,dx::linalg::MatrixLayout::RowMajor);
    B b=B::Load(weights,(262144+group*16384+block*16*64+k*32)*2,128,dx::linalg::MatrixLayout::ColMajor,16);
#if NATIVE_FAST_ACCUMULATE
    acc.MultiplyAccumulate(a,b);
#else
    C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
    for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)+p.Get(i)));
#endif
   }
#if NATIVE_FAST_ACCUMULATE
   for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)));
#endif
#if NATIVE_SPLIT_FFWD_BLOCKED && NATIVE_FAST_EPILOGUE
   for(uint i=0;i<acc.Length();i++){uint2 rc=acc.GetCoordinate(i);hidden[rc.x*272+block*16+rc.y]=float16_t(Activate(acc.Get(i)));}
#elif NATIVE_SPLIT_FFWD_BLOCKED
   for(uint i=0;i<acc.Length();i++){float v=acc.Get(i),g=clamp(v,-4.0,4.0),p=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);uint2 rc=acc.GetCoordinate(i);hidden[rc.x*272+block*16+rc.y]=float16_t(F(H(v*p)));}
#else
   for(uint i=0;i<acc.Length();i++){float v=acc.Get(i),g=clamp(v,-4.0,4.0),p=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);acc.Set(i,F(H(v*p)));}
   acc.Store(temp,0,16,dx::linalg::MatrixLayout::RowMajor);GroupMemoryBarrierWithGroupSync();
   for(uint i=t;i<256;i+=32)hidden[(i/16)*272+block*16+i%16]=float16_t(temp[i]);
   GroupMemoryBarrierWithGroupSync();
#endif
  }
#if NATIVE_SPLIT_FFWD_BLOCKED
  GroupMemoryBarrierWithGroupSync();
  {
   C acc[4];[unroll]for(uint block=0;block<4;block++)acc[block]=C::Splat(0.0f);
   for(uint k=0;k<8;k++){
    A a=A::Load(hidden,k*32,272,dx::linalg::MatrixLayout::RowMajor);
    [unroll]for(uint block=0;block<4;block++){
     B b=B::Load(weights,(393216+group*16384+block*16*256+k*32)*2,512,dx::linalg::MatrixLayout::ColMajor,16);
#if NATIVE_FAST_ACCUMULATE
     acc[block].MultiplyAccumulate(a,b);
#else
     C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
     for(uint i=0;i<acc[block].Length();i++)acc[block].Set(i,H(acc[block].Get(i)+p.Get(i)));
#endif
    }
   }
   [unroll]for(uint block=0;block<4;block++){
#if NATIVE_FAST_ACCUMULATE
    for(uint i=0;i<acc[block].Length();i++)acc[block].Set(i,F(H(acc[block].Get(i))));
#else
    for(uint i=0;i<acc[block].Length();i++)acc[block].Set(i,F(acc[block].Get(i)));
#endif
    acc[block].Store(output,(first*512+group*64+block*16)*4,512*4,dx::linalg::MatrixLayout::RowMajor,16);
   }
  }
#else
  for(uint block=0;block<4;block++){
   C acc=C::Splat(0.0f);
   for(uint k=0;k<8;k++){
    A a=A::Load(hidden,k*32,272,dx::linalg::MatrixLayout::RowMajor);
    B b=B::Load(weights,(393216+group*16384+block*16*256+k*32)*2,512,dx::linalg::MatrixLayout::ColMajor,16);
    C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
    for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)+p.Get(i)));
   }
   for(uint i=0;i<acc.Length();i++)acc.Set(i,F(acc.Get(i)));
   acc.Store(output,(first*512+group*64+block*16)*4,512*4,dx::linalg::MatrixLayout::RowMajor,16);
  }
#endif
  GroupMemoryBarrierWithGroupSync();
 }
}
#endif
