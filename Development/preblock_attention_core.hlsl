#if NATIVE_WAVE_C32_SCORES
#include <dx/linalg.h>
cbuffer RuntimeGeometry:register(b0){uint runtime_seed;uint runtime_width;uint runtime_height;uint local_oracle;uint temporal_enabled;}
#endif
#if NATIVE_PAD_C32_LDS && !NATIVE_WAVE_C32_SCORES
#define LDS_STRIDE 33
#else
#define LDS_STRIDE 32
#endif
#if NATIVE_PAD_WAVE_C32_SCORES
#define SCORE_ROW 33
#define SCORE_WIDE 65
#else
#define SCORE_ROW 32
#define SCORE_WIDE 64
#endif
// Original-layout 32-wide attention, lab 8x8 windows. Not yet live integration.
StructuredBuffer<float> weights:register(t0);
StructuredBuffer<float> input:register(t1);
RWStructuredBuffer<float> output:register(u0);
#if NATIVE_WAVE_C32_SCORES
groupshared float16_t queries[2048],keys[2048];
#if NATIVE_WAVE_C32_AV
groupshared float16_t values[2048];
groupshared float scores[128*SCORE_ROW];
#else
groupshared float values[2048],scores[128*SCORE_ROW];
#endif
#else
groupshared float queries[64*LDS_STRIDE],keys[64*LDS_STRIDE],values[64*LDS_STRIDE];
#endif
#if NATIVE_WAVE_C32_AV
float ReadAux(uint k,uint t){return k<32?float(queries[k*64+t]):float(keys[(k-32)*64+t]);}
void WriteAux(uint k,uint t,float v){if(k<32)queries[k*64+t]=float16_t(v);else keys[(k-32)*64+t]=float16_t(v);}
#define EX(k) ReadAux(k,t)
#else
#define EX(k) ex[k]
#endif
#if NATIVE_C32_CORRECTED_HALF
#include "native_half_corrected.hlsli"
float H(float v){return NativeCorrectedHalf(v);}
#elif NATIVE_C32_HARDWARE_HALF
float H(float v){return f16tof32(f32tof16(v));}
#else
#if NATIVE_C32_BRANCH_HALF
#define HALF_BRANCH [branch]
#else
#define HALF_BRANCH
#endif
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;HALF_BRANCH if(a>=0x7f800000u)return v;HALF_BRANCH if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
#undef HALF_BRANCH
#endif
float LegacyF(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
#if NATIVE_FAST_C32_FP8
#include "native_fp8_fast.hlsli"
float F(float v){[branch]if(!isfinite(v))return LegacyF(v);return NativeFastFp8(v);}
#else
float F(float v){return LegacyF(v);}
#endif
float half_add_preserving_midpoint(float a,float b){
 precise float sum=a+b;
 precise float virtual_b=sum-a;
 precise float error=(a-(sum-virtual_b))+(b-virtual_b);
 uint bits=asuint(sum),magnitude=bits&0x7fffffffu;
 // Preserve the residual when a normal-half midpoint was created by float32
 // rounding. Other cases retain the existing conversion, including subnormals.
 if(magnitude>=0x38800000u&&magnitude<0x47800000u&&(magnitude&0x1fffu)==0x1000u&&error!=0){
  bool increase_bits=(error>0)==(sum>0);
  sum=asfloat(increase_bits?bits+1:bits-1);
 }
 return H(sum);
}
float fast_exp(float x){float a=clamp(H(x*.044921875+1.30078125),1.03125,1.5693359375);uint b=f32tof16(a);return f16tof32(((b<<5)+0x8000u)&65535u);}
#include "native_half_square.hlsli"
#if NATIVE_WAVE_C32_SCORES
[WaveSize(32)]
#endif
[numthreads(64,1,1)]
void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
#if NATIVE_WAVE_C32_SCORES
 if(gid.x*64>=runtime_width*runtime_height)return;
#else
 if(gid.x*64>=TOTAL_OUTPUTS/32)return;
#endif
 uint t=tid.x,p=gid.x*64+t;
 float q[32],k[32],v[32],qs[32],ks[32];
#if NATIVE_WAVE_C32_QKV
 // Phase-local aliases: queries=input, keys=current weight matrix,
 // scores=raw Q/K, values=raw V. Reused after all threads load their rows.
 for(uint j=0;j<32;j++)queries[t*32+j]=float16_t(F(input[p*32+j]));
 GroupMemoryBarrierWithGroupSync();
 using QA=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using QB=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using QC=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 for(uint part=0;part<3;part++){
  for(uint i=t;i<1024;i+=64)keys[i]=float16_t(weights[part*1024+i]);
  GroupMemoryBarrierWithGroupSync();
  for(uint qr=t/32;qr<4;qr+=2)for(uint cr=0;cr<2;cr++){
   QA qa=QA::Load(queries,qr*16*32,32,dx::linalg::MatrixLayout::RowMajor);
   QB wb=QB::Load(keys,cr*16*32,32,dx::linalg::MatrixLayout::ColMajor);
   QC z=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,wb);
   for(uint i=0;i<z.Length();i++)z.Set(i,H(z.Get(i)));
   if(part<2)z.Store(scores,part*64*SCORE_ROW+qr*16*SCORE_ROW+cr*16,SCORE_ROW,dx::linalg::MatrixLayout::RowMajor);
   else {
#if NATIVE_WAVE_C32_AV
    for(uint i=0;i<z.Length();i++){uint2 coord=z.GetCoordinate(i);values[(qr*16+coord.x)*32+cr*16+coord.y]=float16_t(F(z.Get(i)));}
#else
    z.Store(values,qr*16*32+cr*16,32,dx::linalg::MatrixLayout::RowMajor);
#endif
   }
  }
  GroupMemoryBarrierWithGroupSync();
 }
 for(uint c=0;c<32;c++){q[c]=scores[t*SCORE_ROW+c];k[c]=scores[64*SCORE_ROW+t*SCORE_ROW+c];v[c]=F(values[t*32+c]);}
#else
#if NATIVE_CACHE_C32_INPUT
 float cached_input[32];[unroll]for(uint j=0;j<32;j++)cached_input[j]=F(input[p*32+j]);
#endif
 [loop]for(uint c=0;c<32;c++){
  float a=0,b=0,z=0;
  [loop]for(uint j=0;j<32;j++){
#if NATIVE_CACHE_C32_INPUT
   float f=cached_input[j];
#else
   float f=F(input[p*32+j]);
#endif
   a+=f*weights[c*32+j];b+=f*weights[1024+c*32+j];z+=f*weights[2048+c*32+j];}
  q[c]=H(a);k[c]=H(b);v[c]=F(H(z));
 }
#endif
 [unroll]for(uint i=0;i<16;i++){qs[i]=NativeHalfSquarePair(q[i],q[i+16]);ks[i]=NativeHalfSquarePair(k[i],k[i+16]);}
 [unroll]for(uint i=0;i<8;i++){qs[i]=H(qs[i*2]+qs[i*2+1]);ks[i]=H(ks[i*2]+ks[i*2+1]);}
 [unroll]for(uint step=4;step>0;step/=2){[loop]for(uint i=0;i<step;i++){qs[i]=H(qs[i]+qs[i+step]);ks[i]=H(ks[i]+ks[i+step]);}}
 float qi=H(rsqrt(max(qs[0],6.198883056640625e-5))),ki=H(rsqrt(max(ks[0],6.198883056640625e-5)));
 [loop]for(uint c=0;c<32;c++){queries[t*LDS_STRIDE+c]=F(H(H(q[c]*qi)*H(weights[8192])));keys[t*LDS_STRIDE+c]=F(H(k[c]*ki));values[t*LDS_STRIDE+c]=v[c];}
 GroupMemoryBarrierWithGroupSync();
#if NATIVE_WAVE_C32_SCORES
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 for(uint qr=t/32;qr<4;qr+=2)for(uint kr=0;kr<4;kr++){
  A qa=A::Load(queries,qr*16*32,32,dx::linalg::MatrixLayout::RowMajor);
  B kb=B::Load(keys,kr*16*32,32,dx::linalg::MatrixLayout::ColMajor);
  C s=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,kb);
  s.Store(scores,qr*16*SCORE_WIDE+kr*16,SCORE_WIDE,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
#endif
#if !NATIVE_WAVE_C32_AV
 float ex[64],prob[64];
#endif
 [loop]for(uint key=0;key<64;key++){
#if NATIVE_WAVE_C32_SCORES
  float dot=scores[t*SCORE_WIDE+key];
#else
  float dot=0;[loop]for(uint c=0;c<32;c++)dot+=queries[t*LDS_STRIDE+c]*keys[key*LDS_STRIDE+c];
#endif
#if NATIVE_WAVE_C32_AV
  WriteAux(key,t,fast_exp(H(dot+weights[4096+t*64+key])));
#else
  ex[key]=fast_exp(H(dot+weights[4096+t*64+key]));
#endif
 }
 float parity[2];
 [unroll]for(uint odd=0;odd<2;odd++){
  float total=0;
  [unroll]for(uint lane=0;lane<4;lane++){
   uint base=odd+(lane%2)*2+(lane/2)*8;
   float partial=H(EX(base)+EX(base+16));
   partial=H(partial+H(EX(base+4)+EX(base+20)));
   partial=H(partial+H(EX(base+32)+EX(base+48)));
   partial=H(partial+H(EX(base+36)+EX(base+52)));
   total=lane==0?partial:H(total+partial);
  }
  parity[odd]=total;
 }
 float inv=H(1/H(parity[0]+parity[1]));
 float av[32];
#if NATIVE_WAVE_C32_AV
 for(uint key=0;key<64;key++)WriteAux(key,t,F(H(ReadAux(key,t)*inv)));
 GroupMemoryBarrierWithGroupSync();
 for(uint qr=t/32;qr<4;qr+=2)for(uint cr=0;cr<2;cr++){
  C acc=C::Splat(0.0f);
  for(uint g=0;g<2;g++){
   A pa;
   if(g==0)pa=A::Load(queries,qr*16,64,dx::linalg::MatrixLayout::ColMajor);
   else pa=A::Load(keys,qr*16,64,dx::linalg::MatrixLayout::ColMajor);
   B vb=B::Load(values,g*32*32+cr*16,32,dx::linalg::MatrixLayout::RowMajor);
   C partial=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(pa,vb);
   for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)+partial.Get(i)));
  }
  for(uint i=0;i<acc.Length();i++)acc.Set(i,F(acc.Get(i)));
  acc.Store(scores,qr*16*SCORE_ROW+cr*16,SCORE_ROW,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
#if NATIVE_WAVE_C32_PROJECTION
 for(uint c=0;c<32;c++)queries[t*32+c]=float16_t(scores[t*SCORE_ROW+c]);
 for(uint i=t;i<1024;i+=64)keys[i]=float16_t(weights[3072+i]);
 GroupMemoryBarrierWithGroupSync();
 for(uint qr=t/32;qr<4;qr+=2)for(uint cr=0;cr<2;cr++){
  A aa=A::Load(queries,qr*16*32,32,dx::linalg::MatrixLayout::RowMajor);
  B bb=B::Load(keys,cr*16*32,32,dx::linalg::MatrixLayout::ColMajor);
  C z=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(aa,bb);
  z.Store(scores,qr*16*SCORE_ROW+cr*16,SCORE_ROW,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
#else
 for(uint c=0;c<32;c++)av[c]=scores[t*SCORE_ROW+c];
#endif
#else
 [loop]for(uint key=0;key<64;key++)prob[key]=F(H(ex[key]*inv));
 [loop]for(uint c=0;c<32;c++){
  float a=0;[unroll]for(uint group=0;group<2;group++){float s=0;[loop]for(uint key=0;key<32;key++)s+=prob[group*32+key]*values[(group*32+key)*LDS_STRIDE+c];a=H(a+s);}av[c]=F(a);
 }
#endif
 [loop]for(uint c=0;c<32;c++){
#if NATIVE_WAVE_C32_PROJECTION
  float a=scores[t*SCORE_ROW+c];
#else
  float a=0;[loop]for(uint j=0;j<32;j++)a+=av[j]*weights[3072+c*32+j];
#endif
  float result=half_add_preserving_midpoint(a,H(input[p*32+c]*weights[8193+c]));
  output[p*32+c]=RAW_OUTPUT?result:F(result);
 }
}
