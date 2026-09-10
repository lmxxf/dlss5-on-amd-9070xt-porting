// Register-blocked ViT expand (1024->4096) and reduce (4096->1024).
// Same per-element K32 accumulation order and H()/F() as native_wave_vit_expand
// and native_wave_vit_reduce; only data movement changes: each wave computes
// 16 tokens x (16*BLOCK_N) outputs and the hidden layer is stored as f16 (its
// values are F() outputs on the FP8 grid, so the conversion is exact).
#ifndef BLOCK_N
#define BLOCK_N 4
#endif
#include <dx/linalg.h>
#ifndef NATIVE_PACKED_INPUT
#define NATIVE_PACKED_INPUT 0
#endif
#if VIT_EXPAND
#if NATIVE_PACKED_INPUT
ByteAddressBuffer input8:register(t0);
#else
StructuredBuffer<float> input_f32:register(t0);
#endif
#else
#ifndef INPUT_CHANNELS
#define INPUT_CHANNELS 4096
#endif
#if INPUT_CHANNELS==4096
ByteAddressBuffer hidden_f16:register(t0);
#elif NATIVE_PACKED_INPUT
ByteAddressBuffer input8:register(t0);
#else
StructuredBuffer<float> input_f32:register(t0);
#endif
StructuredBuffer<float> residual:register(t2);
#define REDUCE_SRC(i) input_f32[(first+(i)/32)*INPUT_CHANNELS+k+(i)%32]
#endif
ByteAddressBuffer weights:register(t1);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint tokens;uint output_base;}
#ifndef NATIVE_FP8_HIDDEN
#define NATIVE_FP8_HIDDEN NATIVE_FP8_OPERANDS
#endif
#ifndef NATIVE_VIT_TILED
#define NATIVE_VIT_TILED 0
#endif
/* FAST PATH (DLSS5_VIT_TILED): 512-byte contiguous tiles instead of power-of-two row strides (4096-byte strides put the 16 rows
   of every tile on one memory channel). Weights [n out][k in] as tiles (n/16, k/32) of [k 32][j 16] bytes at ((n/16)*(K/32)+k/32)*512;
   the hidden layer [token][4096] as tiles (token/16, k/32) of [16][32] at ((token/16)*128+k/32)*512*HELEM. Pure data movement. */
#define HID_TILE(tok,k) ((((tok)/16)*128+(k)/32)*512*HELEM+((k)%32)*HELEM)
#if NATIVE_FP8_HIDDEN
#define HIDDEN_TYPE dx::linalg::ComponentType::F8_E4M3FN
#define HELEM 1
#else
#define HIDDEN_TYPE dx::linalg::ComponentType::F16
#define HELEM 2
#endif
#if NATIVE_FP8_OPERANDS
// FAST PATH stage 2: E4M3 operands. LDS staging packs four E4M3 bytes per uint, one lane per uint.
#define OPERAND dx::linalg::ComponentType::F8_E4M3FN
#define ELEM 1
groupshared uint tile8[128];
uint E4M3(float v){uint b=asuint(v),a=b&0x7fffffffu,sg=(b>>24)&0x80u;if(a==0)return sg;float m=abs(v);if(m<0.015625)return sg|uint(round(m*512.0));uint e=(a>>23)-127+7,mant=(a>>20)&7u;if(e>15)return sg|0x7eu;return sg|(e<<3)|mant;}
#define STAGE_A(EXPR) for(uint u=tid.x;u<128;u+=32){uint i0=u*4;tile8[u]=E4M3(EXPR(i0))|(E4M3(EXPR(i0+1))<<8)|(E4M3(EXPR(i0+2))<<16)|(E4M3(EXPR(i0+3))<<24);}
#define LOAD_A() A::Load(tile8,0,8,dx::linalg::MatrixLayout::RowMajor) /* stride in packed uints */
#else
#define OPERAND dx::linalg::ComponentType::F16
#define ELEM 2
groupshared float16_t tile[512];
#define STAGE_A(EXPR) for(uint i=tid.x;i<512;i+=32)tile[i]=float16_t(EXPR(i));
#define LOAD_A() A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor)
#endif
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
using A=dx::linalg::Matrix<OPERAND,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<OPERAND,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
#if NATIVE_FAST_EPILOGUE
// FAST PATH stage 3a: activation polynomial without intermediate f16 roundings; single RNE quantization to the FP8 grid.
float Ffast(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
float Activate(float v){float g=clamp(v,-4.0,4.0),p=g*(abs(g)*(-.055908203125)+.447265625)+.89453125;return Ffast(v*p);}
#endif

// input: f32 [tokens][1024]; output: f16 hidden [tokens][4096].
#if VIT_EXPAND
#ifndef BLOCK_M
#define BLOCK_M 1
#endif
#if BLOCK_M>1
// FAST PATH: BLOCK_M token tiles per wave share every B tile load (weight traffic / BLOCK_M). Packed input only.
[WaveSize(32)]
[numthreads(32,1,1)]void expand(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16*BLOCK_M;if(first>=tokens)return;
 C acc[BLOCK_M][BLOCK_N];
 [unroll]for(uint m=0;m<BLOCK_M;m++)[unroll]for(uint n=0;n<BLOCK_N;n++)acc[m][n]=C::Splat(0.0f);
 [loop]for(uint g=0;g<32;g++){
  B b[BLOCK_N];[unroll]for(uint n=0;n<BLOCK_N;n++){uint col=(gid.y*BLOCK_N+n)*16;
#if NATIVE_VIT_TILED
   b[n]=B::Load(weights,((col/16)*32+g)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
#else
   b[n]=B::Load(weights,(col*1024+g*32)*ELEM,1024*ELEM,dx::linalg::MatrixLayout::ColMajor,16);
#endif
  }
  [unroll]for(uint m=0;m<BLOCK_M;m++){
   A a=A::Load(input8,(first+m*16)*1024+g*32,1024,dx::linalg::MatrixLayout::RowMajor,16);
   [unroll]for(uint n=0;n<BLOCK_N;n++)acc[m][n].MultiplyAccumulate(a,b[n]);
  }
 }
 [unroll]for(uint m=0;m<BLOCK_M;m++)[unroll]for(uint n=0;n<BLOCK_N;n++){
  uint col=(gid.y*BLOCK_N+n)*16;
  for(uint i=0;i<acc[m][n].Length();i++)acc[m][n].Set(i,Activate(acc[m][n].Get(i)));
#if NATIVE_VIT_TILED
  acc[m][n].Cast<HIDDEN_TYPE>().Store(output,HID_TILE(first+m*16,col),32*HELEM,dx::linalg::MatrixLayout::RowMajor,16);
#else
  acc[m][n].Cast<HIDDEN_TYPE>().Store(output,((first+m*16)*4096+col)*HELEM,4096*HELEM,dx::linalg::MatrixLayout::RowMajor,16);
#endif
 }
}
#else
[WaveSize(32)]
[numthreads(32,1,1)]void expand(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16;if(first>=tokens)return;
 C acc[BLOCK_N];
 [unroll]for(uint n=0;n<BLOCK_N;n++)acc[n]=C::Splat(0.0f);
 [loop]for(uint g=0;g<32;g++){
#if NATIVE_PACKED_INPUT
  A a=A::Load(input8,first*1024+g*32,1024,dx::linalg::MatrixLayout::RowMajor,16);
#else
#define EXPAND_SRC(i) input_f32[(first+(i)/32)*1024+g*32+(i)%32]
  STAGE_A(EXPAND_SRC)
  GroupMemoryBarrierWithGroupSync();
  A a=LOAD_A();
#endif
  [unroll]for(uint n=0;n<BLOCK_N;n++){
   uint col=(gid.y*BLOCK_N+n)*16;
   B b=B::Load(weights,(col*1024+g*32)*ELEM,1024*ELEM,dx::linalg::MatrixLayout::ColMajor,16);
#if NATIVE_FAST_ACCUMULATE
   acc[n].MultiplyAccumulate(a,b);
#else
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,H(acc[n].Get(i)+p.Get(i)));
#endif
  }
#if !NATIVE_PACKED_INPUT
  GroupMemoryBarrierWithGroupSync();
#endif
 }
 [unroll]for(uint n=0;n<BLOCK_N;n++){
  uint col=(gid.y*BLOCK_N+n)*16;
#if NATIVE_FAST_EPILOGUE
  for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,Activate(acc[n].Get(i)));
#else
  for(uint i=0;i<acc[n].Length();i++){
#if NATIVE_FAST_ACCUMULATE
   float a=H(acc[n].Get(i));
#else
   float a=acc[n].Get(i);
#endif
   float g=clamp(a,-4.0,4.0),p=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);
   acc[n].Set(i,F(H(a*p)));
  }
#endif
#if NATIVE_SCATTER_STORE
  for(uint i=0;i<acc[n].Length();i++){uint2 rc=acc[n].GetCoordinate(i);output.Store<float16_t>(((first+rc.x)*4096+col+rc.y)*2,float16_t(acc[n].Get(i)));}
#else
  acc[n].Cast<HIDDEN_TYPE>().Store(output,(first*4096+col)*HELEM,4096*HELEM,dx::linalg::MatrixLayout::RowMajor,16);
#endif
 }
}
#endif

#else
// input: f16 hidden [tokens][4096]; residual f32 [tokens][1024]; output f32 [tokens][1024].
// Four K partitions accumulated separately then combined, as in the reference.
#ifndef NATIVE_SPLIT_K
#define NATIVE_SPLIT_K 0
#endif
#if NATIVE_SPLIT_K
// FAST PATH: the four K partitions run as separate groups (gid.z) writing f32 partials
// [part][tokens][1024]; `combine` sums them with the scaled residual and quantizes.
// Four times the waves in flight for a 640-token layer whose waves are otherwise 128 dependent K steps long.
[WaveSize(32)]
[numthreads(32,1,1)]void reduce(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
#ifndef BLOCK_M
#define BLOCK_M 1
#endif
 uint first=gid.x*16*BLOCK_M;if(first>=tokens)return;
#if NATIVE_SPLIT_K==2
 [loop]for(uint part=gid.z;part<4;part++){
#else
 {const uint part=gid.z;
#endif
  C acc[BLOCK_M][BLOCK_N];
  [unroll]for(uint m=0;m<BLOCK_M;m++)[unroll]for(uint n=0;n<BLOCK_N;n++)acc[m][n]=C::Splat(0.0f);
  [loop]for(uint k=part*(INPUT_CHANNELS/4);k<(part+1)*(INPUT_CHANNELS/4);k+=32){
   B b[BLOCK_N];[unroll]for(uint n=0;n<BLOCK_N;n++){uint col=(gid.y*BLOCK_N+n)*16;
#if NATIVE_VIT_TILED&&INPUT_CHANNELS==4096
    b[n]=B::Load(weights,((col/16)*128+k/32)*512,16,dx::linalg::MatrixLayout::RowMajor,16);
#else
    b[n]=B::Load(weights,(col*INPUT_CHANNELS+k)*ELEM,INPUT_CHANNELS*ELEM,dx::linalg::MatrixLayout::ColMajor,16);
#endif
   }
   [unroll]for(uint m=0;m<BLOCK_M;m++){
#if INPUT_CHANNELS==4096&&NATIVE_VIT_TILED
    A a=A::Load(hidden_f16,HID_TILE(first+m*16,k),32*HELEM,dx::linalg::MatrixLayout::RowMajor,16);
#elif INPUT_CHANNELS==4096
    A a=A::Load(hidden_f16,((first+m*16)*INPUT_CHANNELS+k)*HELEM,INPUT_CHANNELS*HELEM,dx::linalg::MatrixLayout::RowMajor,16);
#elif NATIVE_PACKED_INPUT
    A a=A::Load(input8,(first+m*16)*INPUT_CHANNELS+k,INPUT_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
#else
    STAGE_A(REDUCE_SRC)
    GroupMemoryBarrierWithGroupSync();
    A a=LOAD_A();
#endif
    [unroll]for(uint n=0;n<BLOCK_N;n++)acc[m][n].MultiplyAccumulate(a,b[n]);
   }
#if INPUT_CHANNELS!=4096 && !NATIVE_PACKED_INPUT
   GroupMemoryBarrierWithGroupSync();
#endif
  }
  [unroll]for(uint m=0;m<BLOCK_M;m++)[unroll]for(uint n=0;n<BLOCK_N;n++)acc[m][n].Store(output,((part*tokens+first+m*16)*1024+(gid.y*BLOCK_N+n)*16)*4,1024*4,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
// combine binds the partial buffer at t0 (the reduce input slot).
#if INPUT_CHANNELS==4096
#define PARTIAL(i) asfloat(hidden_f16.Load((i)*4))
#elif NATIVE_PACKED_INPUT
#define PARTIAL(i) asfloat(input8.Load((i)*4))
#else
#define PARTIAL(i) input_f32[i]
#endif
[numthreads(64,1,1)]void combine(uint3 id:SV_DispatchThreadID){
 uint idx=id.x*4;if(idx>=tokens*1024)return;uint token=idx/1024,row=idx%1024;
 float4 v;
 [unroll]for(uint j=0;j<4;j++){
  float sum=H(residual[idx+j]*asfloat(weights.Load(INPUT_CHANNELS*1024*ELEM+(row+j)*4)));
  [unroll]for(uint part=0;part<4;part++)sum+=PARTIAL((part*tokens+token)*1024+row+j);
  v[j]=F(H(sum));
 }
 output.Store4(idx*4,asuint(v));
}
#else
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
    for(uint i=0;i<acc[n].Length();i++){uint2 rc=acc[n].GetCoordinate(i);uint row=col+rc.y;acc[n].Set(i,H(residual[(first+rc.x)*1024+row]*asfloat(weights.Load(INPUT_CHANNELS*1024*ELEM+row*4))));}
   }
  }
  [loop]for(uint k=part*(INPUT_CHANNELS/4);k<(part+1)*(INPUT_CHANNELS/4);k+=32){
#if INPUT_CHANNELS==4096
   A a=A::Load(hidden_f16,(first*INPUT_CHANNELS+k)*HELEM,INPUT_CHANNELS*HELEM,dx::linalg::MatrixLayout::RowMajor,16);
#elif NATIVE_PACKED_INPUT
   A a=A::Load(input8,first*INPUT_CHANNELS+k,INPUT_CHANNELS,dx::linalg::MatrixLayout::RowMajor,16);
#else
   STAGE_A(REDUCE_SRC)
   GroupMemoryBarrierWithGroupSync();
   A a=LOAD_A();
#endif
   [unroll]for(uint n=0;n<BLOCK_N;n++){
    uint col=(gid.y*BLOCK_N+n)*16;
    B b=B::Load(weights,(col*INPUT_CHANNELS+k)*ELEM,INPUT_CHANNELS*ELEM,dx::linalg::MatrixLayout::ColMajor,16);
#if NATIVE_FAST_ACCUMULATE
    acc[n].MultiplyAccumulate(a,b);
#else
    C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
    for(uint i=0;i<acc[n].Length();i++)acc[n].Set(i,H(acc[n].Get(i)+p.Get(i)));
#endif
   }
#if INPUT_CHANNELS!=4096 && !NATIVE_PACKED_INPUT
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
#endif
