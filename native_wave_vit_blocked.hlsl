// Register-blocked ViT expand (1024->4096) and reduce (4096->1024).
// Same per-element K32 accumulation order and H()/F() as native_wave_vit_expand
// and native_wave_vit_reduce; only data movement changes: each wave computes
// 16 tokens x (16*BLOCK_N) outputs and the hidden layer is stored as f16 (its
// values are F() outputs on the FP8 grid, so the conversion is exact).
#ifndef BLOCK_N
#define BLOCK_N 4
#endif
#include <dx/linalg.h>
#if VIT_EXPAND
StructuredBuffer<float> input_f32:register(t0);
#else
#ifndef INPUT_CHANNELS
#define INPUT_CHANNELS 4096
#endif
#if INPUT_CHANNELS==4096
ByteAddressBuffer hidden_f16:register(t0);
#else
StructuredBuffer<float> input_f32:register(t0);
#endif
StructuredBuffer<float> residual:register(t2);
#endif
ByteAddressBuffer weights:register(t1);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint tokens;uint output_base;}
groupshared float16_t tile[512];
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;

// input: f32 [tokens][1024]; output: f16 hidden [tokens][4096].
#if VIT_EXPAND
[WaveSize(32)]
[numthreads(32,1,1)]void expand(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16;if(first>=tokens)return;
 C acc[BLOCK_N];
 [unroll]for(uint n=0;n<BLOCK_N;n++)acc[n]=C::Splat(0.0f);
 [loop]for(uint g=0;g<32;g++){
  for(uint i=tid.x;i<512;i+=32)tile[i]=float16_t(input_f32[(first+i/32)*1024+g*32+i%32]);
  GroupMemoryBarrierWithGroupSync();
  A a=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
  [unroll]for(uint n=0;n<BLOCK_N;n++){
   uint col=(gid.y*BLOCK_N+n)*16;
   B b=B::Load(weights,(col*1024+g*32)*2,2048,dx::linalg::MatrixLayout::ColMajor,16);
#if NATIVE_FAST_ACCUMULATE
   acc[n].MultiplyAccumulate(a,b);
#else
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,H(acc[n].Get(i)+p.Get(i)));
#endif
  }
  GroupMemoryBarrierWithGroupSync();
 }
 [unroll]for(uint n=0;n<BLOCK_N;n++){
  uint col=(gid.y*BLOCK_N+n)*16;
  for(uint i=0;i<acc[n].Length();i++){
#if NATIVE_FAST_ACCUMULATE
   float a=H(acc[n].Get(i));
#else
   float a=acc[n].Get(i);
#endif
   float g=clamp(a,-4.0,4.0),p=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);
   acc[n].Set(i,F(H(a*p)));
  }
#if NATIVE_SCATTER_STORE
  for(uint i=0;i<acc[n].Length();i++){uint2 rc=acc[n].GetCoordinate(i);output.Store<float16_t>(((first+rc.x)*4096+col+rc.y)*2,float16_t(acc[n].Get(i)));}
#else
  acc[n].Cast<dx::linalg::ComponentType::F16>().Store(output,(first*4096+col)*2,4096*2,dx::linalg::MatrixLayout::RowMajor,16);
#endif
 }
}

#else
// input: f16 hidden [tokens][4096]; residual f32 [tokens][1024]; output f32 [tokens][1024].
// Four K partitions accumulated separately then combined, as in the reference.
[WaveSize(32)]
[numthreads(32,1,1)]void reduce(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16;if(first>=tokens)return;
 C total[BLOCK_N];
 [loop]for(uint part=0;part<4;part++){
  C acc[BLOCK_N];
  [unroll]for(uint n=0;n<BLOCK_N;n++){
   acc[n]=C::Splat(0.0f);
   if(part==0){
    uint col=(gid.y*BLOCK_N+n)*16;
    for(uint i=0;i<acc[n].Length();i++){uint2 rc=acc[n].GetCoordinate(i);uint row=col+rc.y;acc[n].Set(i,H(residual[(first+rc.x)*1024+row]*asfloat(weights.Load(INPUT_CHANNELS*1024*2+row*4))));}
   }
  }
  [loop]for(uint k=part*(INPUT_CHANNELS/4);k<(part+1)*(INPUT_CHANNELS/4);k+=32){
#if INPUT_CHANNELS==4096
   A a=A::Load(hidden_f16,(first*INPUT_CHANNELS+k)*2,INPUT_CHANNELS*2,dx::linalg::MatrixLayout::RowMajor,16);
#else
   for(uint i=tid.x;i<512;i+=32)tile[i]=float16_t(input_f32[(first+i/32)*INPUT_CHANNELS+k+i%32]);
   GroupMemoryBarrierWithGroupSync();
   A a=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
#endif
   [unroll]for(uint n=0;n<BLOCK_N;n++){
    uint col=(gid.y*BLOCK_N+n)*16;
    B b=B::Load(weights,(col*INPUT_CHANNELS+k)*2,INPUT_CHANNELS*2,dx::linalg::MatrixLayout::ColMajor,16);
#if NATIVE_FAST_ACCUMULATE
    acc[n].MultiplyAccumulate(a,b);
#else
    C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
    for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,H(acc[n].Get(i)+p.Get(i)));
#endif
   }
#if INPUT_CHANNELS!=4096
   GroupMemoryBarrierWithGroupSync();
#endif
  }
#if NATIVE_FAST_ACCUMULATE
  // Partitions are summed in FP32 without intermediate rounding.
  [unroll]for(uint n=0;n<BLOCK_N;n++){if(part==0)total[n]=acc[n];else for(uint i=0;i<total[n].Length();i++)total[n].Set(i,total[n].Get(i)+acc[n].Get(i));}
#else
  [unroll]for(uint n=0;n<BLOCK_N;n++){if(part==0)total[n]=acc[n];else for(uint i=0;i<total[n].Length();i++)total[n].Set(i,H(total[n].Get(i)+acc[n].Get(i)));}
#endif
 }
 [unroll]for(uint n=0;n<BLOCK_N;n++){
#if NATIVE_FAST_ACCUMULATE
  for(uint i=0;i<total[n].Length();i++)total[n].Set(i,F(H(total[n].Get(i))));
#else
  for(uint i=0;i<total[n].Length();i++)total[n].Set(i,F(total[n].Get(i)));
#endif
  total[n].Store(output,(first*1024+(gid.y*BLOCK_N+n)*16)*4,1024*4,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
#endif
