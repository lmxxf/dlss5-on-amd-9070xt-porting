// Register-blocked C32 FFN (32 -> 128 -> 32 with residual), same bindings and
// numerics as native_wave_c32_ffn_local.hlsl: identical K32 order, H() per step,
// F() on the hidden layer, initial residual H(input*scale). Only the LDS
// round trips are removed: expand keeps 8 accumulators and writes the f16
// hidden tile once; contract keeps 2 accumulators over 4 K steps.
#include <dx/linalg.h>
#if NATIVE_STATIC_LENGTH
// 16x16 f32 accumulator on wave32 = 8 elements per lane; static trip count lets Get/Set index registers statically.
#define ELEM_LOOP(m) [unroll]for(uint i=0;i<8;i++)
#else
#define ELEM_LOOP(m) for(uint i=0;i<m.Length();i++)
#endif
ByteAddressBuffer weights:register(t0);
#ifndef NATIVE_C32_FFN_FAST3
#define NATIVE_C32_FFN_FAST3 0
#endif
#if NATIVE_C32_FFN_FAST3
// FAST PATH 3: the f32 input tile is loaded as two 16x16 accumulator matrices (residual) and quantized to E4M3 through
// the hardware Cast into LDS (A operand); expand runs FP8 x FP8 on the E4M3 expand-weight copy at byte 20608.
// Requires FAST2 + FP8 and the root-descriptor (raw store) binding; only the identity mapping (map_mode 0) takes this path.
ByteAddressBuffer input_bytes:register(t1);
#define input_at(i) asfloat(input_bytes.Load((i)*4))
// Mode 4 (post70 merge fold): input = low-res main [pixel/4][32], skip = full-res residual, merge_w = 64 coefficients;
// value = H(H(low*w[c]) + skip*w[32+c]) exactly as native_post70.hlsl merge, computed in the gather instead of a pass.
ByteAddressBuffer skip_bytes:register(t2);
ByteAddressBuffer merge_w:register(t3);
// Mode 5 (pre block, inline prefix): the input-mix prefix (16 features -> 32 channels) is computed here from the RGB tiles
// (t1, f32 [pixel][4]) and the temporal history (t3 root SRV, float4 per raster pixel); prefix weights as f16 B tiles at byte 27776.
uint pcg(uint s){uint w=((s>>((s>>28)+4))^s)*0x108ef2d9;return (w>>22)^w;}
float uniform24(uint s){uint w=((s>>((s>>28)+4))^s)*0x108ef2d9;return float(((w>>30)^(w>>8))+1)*5.9604644775390625e-8;}
float half_round(float v){return f16tof32(f32tof16(v));}
#else
StructuredBuffer<float> input:register(t1);
#define input_at(i) input[i]
#endif
#if NATIVE_C32_FFN_FAST2
RWByteAddressBuffer output:register(u0);
#else
RWStructuredBuffer<float> output:register(u0);
#endif
cbuffer Geometry:register(b0){uint seed;uint width;uint height;uint local_oracle;uint temporal;
 // FAST PATH input mapping (NATIVE_C32_MAPPED_INPUT): 0 = tile-major work buffer as-is; 1 = HWC raster source;
 // 2 = previous stage's Main() (row-major over its own shifted work grid). Border tokens read zero.
 uint map_mode;uint src_width;uint src_height;uint shift_x;uint shift_y;uint prev_shift_x;uint prev_shift_y;uint prev_work_width;}
#ifndef NATIVE_C32_MAPPED_INPUT
#define NATIVE_C32_MAPPED_INPUT 0
#endif
#if NATIVE_C32_MAPPED_INPUT
int source_index(uint p){
 if(map_mode==0)return int(p*32);
 uint tile=p/64,x=(tile%(width/8))*8+p%8,y=(tile/(width/8))*8+(p%64)/8;
 int sx=int(x)-int(shift_x),sy=int(y)-int(shift_y);
 if(sx<0||sy<0||sx>=int(src_width)||sy>=int(src_height))return -1;
 if(map_mode==1||map_mode==4)return int((uint(sy)*src_width+uint(sx))*32);
 // Main() of the previous stage is row-major over its (shifted) work grid.
 uint px=uint(sx)+prev_shift_x,py=uint(sy)+prev_shift_y;
 if(map_mode==2)return int((py*prev_work_width+px)*32);
 // Mode 3 (FAST PATH 3 chain): the previous stage's raw tiles (tile-major over its work grid, unquantized);
 // the FFN quantizes for both the A operand and the residual, so the previous finish stage can be skipped.
 uint ptile=(py/8)*(prev_work_width/8)+px/8;return int((ptile*64+(py%8)*8+px%8)*32);
}
#endif
groupshared float16_t prefix[512],hidden[2048];
#ifndef NATIVE_C32_FFN_FAST2
#define NATIVE_C32_FFN_FAST2 0
#endif
#if NATIVE_C32_FFN_FAST2
groupshared float raw[512];
#endif
#ifndef NATIVE_C32_FFN_FP8
#define NATIVE_C32_FFN_FP8 0
#endif
// FAST PATH: B tiles contiguous — expand f16 tile `block` (32 K-rows x 16 halves) at block*1024; contract E4M3 tile (block,g) at 16512+(block*4+g)*512.
#ifndef NATIVE_C32_TILED_WEIGHTS
#define NATIVE_C32_TILED_WEIGHTS 0
#endif
#if NATIVE_C32_FFN_FP8
// FAST PATH: hidden layer as E4M3 through the hardware cast (no scalar Ffast); contract weights are an FP8 copy at byte 16512.
groupshared uint hidden8[512];
#if NATIVE_C32_FFN_FAST3
groupshared uint prefix8[128];
#endif
float ActivatePoly(float v){float g=clamp(v,-4.0,4.0),p=g*(abs(g)*(-.055908203125)+.447265625)+.89453125;return v*p;}
#endif
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
#if NATIVE_FAST_F
// Bit-level equivalent of the legacy F for finite inputs: round-to-nearest-even on the
// 3-bit mantissa (HLSL round() is RNE), same subnormal path, same 448 clamp.
float F(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
#else
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
#endif
#if NATIVE_FAST_EPILOGUE
// FAST PATH stage 3a: activation polynomial without intermediate f16 roundings; single RNE quantization to the FP8 grid.
float Ffast(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
float Activate(float v){float g=clamp(v,-4.0,4.0),p=g*(abs(g)*(-.055908203125)+.447265625)+.89453125;return Ffast(v*p);}
#endif
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=(gid.x+gid.y*65535u)*16,t=tid.x;if(first>=width*height)return;
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
#if NATIVE_C32_FFN_FAST3
 {
  using A8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
  using B8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
  C in0,in1;
#if NATIVE_C32_MAPPED_INPUT
  [branch]if(map_mode==0){
#endif
   in0=C::Load(input_bytes,first*128,128,dx::linalg::MatrixLayout::RowMajor,16);in1=C::Load(input_bytes,first*128+64,128,dx::linalg::MatrixLayout::RowMajor,16);
#if NATIVE_C32_MAPPED_INPUT
  }else{
   // Mapped source: gather the 16 token rows into LDS (lane j<16 resolves token j once), then load them as accumulators.
   const int mine=t<16?source_index(first+t):0;
   [branch]if(map_mode==5){
    if(t<16){
     const uint p=first+t;
     uint x=p%8,y=(p/8)%8;
     if(!local_oracle){uint tile=p/64;x=(tile%(width/8))*8+p%8;y=(tile/(width/8))*8+(p%64)/8;}
     uint h=pcg((x*0x8da6b343)^(y*0xd8163841)^(seed*0x9e3779b9u)^0x243f6a88u);
     float a=uniform24(h*0xcaa5b80d+0x21dd796b),b=uniform24(h*0x2c9277b5+0xac564b05);
     float c=uniform24(h*0x83232c31+0x3463e0ac),d=uniform24(h*0xfa6dc5f9+0x4712a88e);
     float r0=sqrt(-2*log(a)),r1=sqrt(-2*log(b));
     float g0=half_round(r0*cos(6.283185482025146*c)),g1=half_round(r1*cos(6.283185482025146*d)),g2=half_round(r1*sin(6.283185482025146*d));
     float r=half_round(half_round(half_round(input_at(p*4))-0.5)*.125),g=half_round(half_round(half_round(input_at(p*4+1))-0.5)*.125),bl=half_round(half_round(half_round(input_at(p*4+2))-0.5)*.125);
     float features[16]={g1,g2,g,bl,g0,1,.0078125,1,r,g,1,1,bl,r,1,0};
     if(temporal){uint tile=p/64;uint tx=(tile%(width/8))*8+p%8,ty=(tile/(width/8))*8+(p%64)/8;float3 hist=asfloat(merge_w.Load3((ty*width+tx)*16));
      features[13]=half_round(half_round(half_round(hist.x)-.5)*.125);features[2]=half_round(half_round(half_round(hist.y)-.5)*.125);features[3]=half_round(half_round(half_round(hist.z)-.5)*.125);}
     [unroll]for(uint i=0;i<16;i++)prefix[t*32+i]=float16_t(features[i]);
     [unroll]for(uint i=16;i<32;i++)prefix[t*32+i]=float16_t(0.0);
    }
    GroupMemoryBarrierWithGroupSync();
    A fa=A::Load(prefix,0,32,dx::linalg::MatrixLayout::RowMajor);
    B b0=B::Load(weights,27776,32,dx::linalg::MatrixLayout::RowMajor,16),b1=B::Load(weights,27776+1024,32,dx::linalg::MatrixLayout::RowMajor,16);
    in0=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(fa,b0);in1=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(fa,b1);
    ELEM_LOOP(in0){in0.Set(i,half_round(in0.Get(i)));in1.Set(i,half_round(in1.Get(i)));}
   }else{
   [branch]if(map_mode==4){
    const float w0=asfloat(merge_w.Load(t*4)),w1=asfloat(merge_w.Load((32+t)*4));
    [unroll]for(uint j=0;j<16;j++){int src=WaveReadLaneAt(mine,j);float v=0;
     if(src>=0){uint p=uint(src)/32,low=(((p/src_width)/2)*(src_width/2)+(p%src_width)/2)*32+t;v=f16tof32(f32tof16(f16tof32(f32tof16(input_at(low)*w0))+asfloat(skip_bytes.Load((uint(src)+t)*4))*w1));}
     raw[j*32+t]=v;}
   }else{
   [unroll]for(uint j=0;j<16;j++){int src=WaveReadLaneAt(mine,j);raw[j*32+t]=src<0?0:input_at(uint(src)+t);}
   }
   GroupMemoryBarrierWithGroupSync();
   in0=C::Load(raw,0,32,dx::linalg::MatrixLayout::RowMajor);in1=C::Load(raw,16,32,dx::linalg::MatrixLayout::RowMajor);
   }
  }
#endif
  in0.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(prefix8,0,8,dx::linalg::MatrixLayout::RowMajor);
  in1.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(prefix8,4,8,dx::linalg::MatrixLayout::RowMajor);
  GroupMemoryBarrierWithGroupSync();
  {
   A8 a=A8::Load(prefix8,0,8,dx::linalg::MatrixLayout::RowMajor);
   [unroll]for(uint block=0;block<8;block++){
#if NATIVE_C32_TILED_WEIGHTS
    B8 b=B8::Load(weights,20608+block*512,16,dx::linalg::MatrixLayout::RowMajor,16);
#else
    B8 b=B8::Load(weights,20608+block*16*32,32,dx::linalg::MatrixLayout::ColMajor,16);
#endif
    C h=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
    ELEM_LOOP(h)h.Set(i,ActivatePoly(h.Get(i)));
    h.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(hidden8,block*4,32,dx::linalg::MatrixLayout::RowMajor);
   }
  }
  GroupMemoryBarrierWithGroupSync();
  C acc[2];
#if NATIVE_C32_MAPPED_INPUT
  [branch]if(map_mode==3){
   // Residual = F(input)*scale as three E4M3 MMAs on the quantized tile (scale = s0+s1+s2, diagonals packed at byte 24704).
   acc[0]=C::Splat(0.0f);acc[1]=C::Splat(0.0f);
   A8 q=A8::Load(prefix8,0,8,dx::linalg::MatrixLayout::RowMajor);
   [unroll]for(uint block=0;block<2;block++)[unroll]for(uint part=0;part<3;part++){B8 d=B8::Load(weights,24704+(block*3+part)*512,16,dx::linalg::MatrixLayout::RowMajor,16);acc[block].MultiplyAccumulate(q,d);}
  }else
#endif
  ELEM_LOOP(acc[0]){uint2 rc=acc[0].GetCoordinate(i);acc[0].Set(i,in0.Get(i)*asfloat(weights.Load(16384+rc.y*4)));acc[1].Set(i,in1.Get(i)*asfloat(weights.Load(16384+64+rc.y*4)));}
  for(uint g=0;g<4;g++){
   A8 a=A8::Load(hidden8,g*8,32,dx::linalg::MatrixLayout::RowMajor);
   [unroll]for(uint block=0;block<2;block++){
#if NATIVE_C32_TILED_WEIGHTS
    B8 b=B8::Load(weights,16512+(block*4+g)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
#else
    B8 b=B8::Load(weights,16512+block*16*128+g*32,128,dx::linalg::MatrixLayout::ColMajor,16);
#endif
    acc[block].MultiplyAccumulate(a,b);
   }
  }
  [unroll]for(uint block=0;block<2;block++){ELEM_LOOP(acc[block])acc[block].Set(i,f16tof32(f32tof16(acc[block].Get(i))));acc[block].Store(output,(first*32+block*16)*4,128,dx::linalg::MatrixLayout::RowMajor,16);}
  return;
 }
#endif
#if NATIVE_C32_FFN_FAST2
 // FAST PATH: raw input tile kept in LDS for the residual; quantized copy for the A operand.
#if NATIVE_C32_MAPPED_INPUT
 [branch]if(map_mode==0){for(uint i=t;i<512;i+=32){float v=input_at(first*32+i);raw[i]=v;prefix[i]=float16_t(Ffast(v));}}
 else{
  // Lane j<16 resolves token j's source once; the staging loop reads it back with a uniform lane index.
  const int mine=t<16?source_index(first+t):0;
  [unroll]for(uint j=0;j<16;j++){int src=WaveReadLaneAt(mine,j);uint i=j*32+t;float v=src<0?0:input_at(uint(src)+t);raw[i]=v;prefix[i]=float16_t(Ffast(v));}
 }
#else
 for(uint i=t;i<512;i+=32){float v=input_at(first*32+i);raw[i]=v;prefix[i]=float16_t(Ffast(v));}
#endif
#else
 for(uint i=t;i<512;i+=32)prefix[i]=float16_t(F(input_at(first*32+i)));
#endif
 GroupMemoryBarrierWithGroupSync();
 {
  A a=A::Load(prefix,0,32,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint block=0;block<8;block++){
#if NATIVE_C32_TILED_WEIGHTS
   B b=B::Load(weights,block*1024,32,dx::linalg::MatrixLayout::RowMajor,16);
#else
   B b=B::Load(weights,block*16*32*2,64,dx::linalg::MatrixLayout::ColMajor,16);
#endif
   C h=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
#if NATIVE_C32_FFN_FP8
   ELEM_LOOP(h)h.Set(i,ActivatePoly(h.Get(i)));
   h.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(hidden8,block*4,32,dx::linalg::MatrixLayout::RowMajor);
#elif NATIVE_C32_FFN_FAST2
   ELEM_LOOP(h)h.Set(i,Activate(h.Get(i)));
   h.Cast<dx::linalg::ComponentType::F16>().Store(hidden,block*16,128,dx::linalg::MatrixLayout::RowMajor);
#elif NATIVE_FAST_EPILOGUE
   ELEM_LOOP(h){uint2 rc=h.GetCoordinate(i);hidden[rc.x*128+block*16+rc.y]=float16_t(Activate(h.Get(i)));}
#else
   ELEM_LOOP(h){float v=H(h.Get(i)),g=clamp(v,-4.0,4.0),p=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);uint2 rc=h.GetCoordinate(i);hidden[rc.x*128+block*16+rc.y]=float16_t(F(H(v*p)));}
#endif
  }
 }
 GroupMemoryBarrierWithGroupSync();
 C acc[2];
 [unroll]for(uint block=0;block<2;block++){
  acc[block]=C::Splat(0.0f);
#if NATIVE_C32_FFN_FAST2
  ELEM_LOOP(acc[block]){uint2 rc=acc[block].GetCoordinate(i);uint c=block*16+rc.y;acc[block].Set(i,raw[rc.x*32+c]*asfloat(weights.Load(16384+c*4)));}
#else
  ELEM_LOOP(acc[block]){uint2 rc=acc[block].GetCoordinate(i);uint c=block*16+rc.y;acc[block].Set(i,H(input_at((first+rc.x)*32+c)*asfloat(weights.Load(16384+c*4))));}
#endif
 }
 for(uint g=0;g<4;g++){
#if NATIVE_C32_FFN_FP8
  using A8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
  using B8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
  A8 a=A8::Load(hidden8,g*8,32,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint block=0;block<2;block++){
#if NATIVE_C32_TILED_WEIGHTS
   B8 b=B8::Load(weights,16512+(block*4+g)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
#else
   B8 b=B8::Load(weights,16512+block*16*128+g*32,128,dx::linalg::MatrixLayout::ColMajor,16);
#endif
#else
  A a=A::Load(hidden,g*32,128,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint block=0;block<2;block++){
   B b=B::Load(weights,8192+(block*16*128+g*32)*2,256,dx::linalg::MatrixLayout::ColMajor,16);
#endif
#if NATIVE_FAST_ACCUMULATE
   acc[block].MultiplyAccumulate(a,b);
#else
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   ELEM_LOOP(acc[block])acc[block].Set(i,H(acc[block].Get(i)+p.Get(i)));
#endif
  }
 }
#if NATIVE_C32_FFN_FAST2
 // Output stays f32 (RAW) for the following stage; f16 rounding through a hardware cast pair.
 [unroll]for(uint block=0;block<2;block++){ELEM_LOOP(acc[block])acc[block].Set(i,f16tof32(f32tof16(acc[block].Get(i))));acc[block].Store(output,(first*32+block*16)*4,128,dx::linalg::MatrixLayout::RowMajor,16);}
}
#else
#if NATIVE_FAST_ACCUMULATE
 [unroll]for(uint block=0;block<2;block++)ELEM_LOOP(acc[block])acc[block].Set(i,H(acc[block].Get(i)));
#endif
 [unroll]for(uint block=0;block<2;block++)ELEM_LOOP(acc[block]){uint2 rc=acc[block].GetCoordinate(i);output[(first+rc.x)*32+block*16+rc.y]=acc[block].Get(i);}
}
#endif
