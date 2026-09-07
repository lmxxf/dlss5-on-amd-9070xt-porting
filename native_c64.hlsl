#if NATIVE_MULTIHEAD_FOUR_WAVES
#if !NATIVE_WAVE_AV || !NATIVE_WAVE_SCORES
#error Parallel multihead requires complete Wave attention
#endif
#if NATIVE_MULTIHEAD_EIGHT_WAVES
#define MULTIHEAD_THREADS 256
#define MULTIHEAD_QUERY (t/64)
#define MULTIHEAD_QSTEP 4
#define MULTIHEAD_COL_START ((t/32)&1)
#define MULTIHEAD_COL_STEP 2
#else
#define MULTIHEAD_THREADS 128
#endif
#else
#define MULTIHEAD_THREADS 64
#endif
#ifndef MULTIHEAD_QUERY
#define MULTIHEAD_QUERY (t/32)
#define MULTIHEAD_QSTEP (MULTIHEAD_THREADS/32)
#define MULTIHEAD_COL_START 0
#define MULTIHEAD_COL_STEP 1
#endif
#if NATIVE_WAVE_AV && !NATIVE_WAVE_SCORES
#error Wave AV requires Wave scores
#endif
#if NATIVE_WAVE_SCORES
#include <dx/linalg.h>
#endif
#ifndef CHANNELS
#define CHANNELS 64
#endif
#define HEADS (CHANNELS/32)
#define MATRIX (CHANNELS*CHANNELS)
#define SCALE_OFFSET (4*MATRIX+HEADS*4096)
#ifndef RAW_OUTPUT
#define RAW_OUTPUT 0
#endif
StructuredBuffer<float> input:register(t0),weights:register(t1),feature:register(t2);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;}
#if NATIVE_PARALLEL_MULTIHEAD_SOFTMAX
#if !NATIVE_WAVE_AV
#error Parallel softmax requires Wave AV
#endif
groupshared float softmax_inverse[64];
#endif
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float LegacyF(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
#if NATIVE_FAST_FP8
#include "native_fp8_fast.hlsli"
float F(float v){[branch]if(!isfinite(v))return LegacyF(v);return NativeFastFp8(v);}
#else
float F(float v){return LegacyF(v);}
#endif
[numthreads(64,1,1)]void ffn(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=width*height)return;float hidden[4*CHANNELS],middle[CHANNELS];
 [loop]for(uint row=0;row<4*CHANNELS;row++){
  float a=0;[unroll]for(uint g=0;g<HEADS;g++){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[p*CHANNELS+g*32+j]*weights[row*CHANNELS+g*32+j];a=H(a+s);}
  float gate=clamp(a,-4.0,4.0),poly=H(gate*H(abs(gate)*(-.055908203125)+.447265625)+.89453125);hidden[row]=F(H(a*poly));
 }
 [loop]for(uint row=0;row<CHANNELS;row++){
  float a=0;[loop]for(uint g=0;g<(4*CHANNELS/32);g++){float s=0;[loop]for(uint j=0;j<32;j++)s+=hidden[g*32+j]*weights[4*MATRIX+row*(4*CHANNELS)+g*32+j];a=H(a+s);}middle[row]=F(a);
 }
 [loop]for(uint row=0;row<CHANNELS;row++){
  float a=H(input[p*CHANNELS+row]*weights[9*MATRIX+row]);[unroll]for(uint g=0;g<HEADS;g++){float s=0;[loop]for(uint j=0;j<32;j++)s+=middle[g*32+j]*weights[8*MATRIX+row*CHANNELS+g*32+j];a=H(a+s);}output[p*CHANNELS+row]=F(a);
 }
}
// Experimental split FFN: each thread computes one output channel rather than
// carrying hidden[4*CHANNELS] and middle[CHANNELS] for a whole pixel.
[numthreads(64,1,1)]void split_ffn_expand(uint3 id:SV_DispatchThreadID){
 uint n=id.x+id.y*4194240u;if(n>=width*height*4*CHANNELS)return;
 uint p=n/(4*CHANNELS),row=n%(4*CHANNELS);float a=0;
 [unroll]for(uint g=0;g<HEADS;g++){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[p*CHANNELS+g*32+j]*weights[row*CHANNELS+g*32+j];a=H(a+s);}
 float gate=clamp(a,-4.0,4.0),poly=H(gate*H(abs(gate)*(-.055908203125)+.447265625)+.89453125);
 output[n]=F(H(a*poly));
}
[numthreads(64,1,1)]void split_ffn_contract(uint3 id:SV_DispatchThreadID){
 uint n=id.x+id.y*4194240u;if(n>=width*height*CHANNELS)return;
 uint p=n/CHANNELS,row=n%CHANNELS;float a=0;
 [loop]for(uint g=0;g<4*HEADS;g++){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[p*4*CHANNELS+g*32+j]*weights[4*MATRIX+row*4*CHANNELS+g*32+j];a=H(a+s);}
 output[n]=F(a);
}
groupshared float ffn_tile_input[8*33],ffn_tile_weight[32*33];
[numthreads(64,1,1)]void tiled_ffn_expand(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 if(gid.x*32>=4*CHANNELS||gid.y*8>=width*height)return;
 uint t=tid.x,local_pixel=t/8,local_row=(t%8)*4;
 uint p=gid.y*8+local_pixel,row=gid.x*32+local_row;float4 a=0;
 [loop]for(uint g=0;g<HEADS;g++){
  [loop]for(uint i=t;i<8*32;i+=64)ffn_tile_input[(i/32)*33+i%32]=input[(gid.y*8+i/32)*CHANNELS+g*32+i%32];
  [loop]for(uint i=t;i<32*32;i+=64)ffn_tile_weight[(i%32)*33+i/32]=weights[(gid.x*32+i/32)*CHANNELS+g*32+i%32];
  GroupMemoryBarrierWithGroupSync();float4 s=0;
  [loop]for(uint j=0;j<32;j++){float v=ffn_tile_input[local_pixel*33+j];uint w=j*33+local_row;s+=v*float4(ffn_tile_weight[w],ffn_tile_weight[w+1],ffn_tile_weight[w+2],ffn_tile_weight[w+3]);}
  a=float4(H(a.x+s.x),H(a.y+s.y),H(a.z+s.z),H(a.w+s.w));GroupMemoryBarrierWithGroupSync();
 }
 [unroll]for(uint r=0;r<4;r++){float gate=clamp(a[r],-4.0,4.0),poly=H(gate*H(abs(gate)*(-.055908203125)+.447265625)+.89453125);output[p*4*CHANNELS+row+r]=F(H(a[r]*poly));}
}
[numthreads(64,1,1)]void tiled_ffn_contract(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 if(gid.x*32>=CHANNELS||gid.y*8>=width*height)return;
 uint t=tid.x,local_pixel=t/8,local_row=(t%8)*4;
 uint p=gid.y*8+local_pixel,row=gid.x*32+local_row;float4 a=0;
 [loop]for(uint g=0;g<4*HEADS;g++){
  [loop]for(uint i=t;i<8*32;i+=64)ffn_tile_input[(i/32)*33+i%32]=input[(gid.y*8+i/32)*4*CHANNELS+g*32+i%32];
  [loop]for(uint i=t;i<32*32;i+=64)ffn_tile_weight[(i%32)*33+i/32]=weights[4*MATRIX+(gid.x*32+i/32)*4*CHANNELS+g*32+i%32];
  GroupMemoryBarrierWithGroupSync();float4 s=0;
  [loop]for(uint j=0;j<32;j++){
   float v=ffn_tile_input[local_pixel*33+j];uint w=j*33+local_row;
   s+=v*float4(ffn_tile_weight[w],ffn_tile_weight[w+1],ffn_tile_weight[w+2],ffn_tile_weight[w+3]);
  }
  a=float4(H(a.x+s.x),H(a.y+s.y),H(a.z+s.z),H(a.w+s.w));
  GroupMemoryBarrierWithGroupSync();
 }
 [unroll]for(uint r=0;r<4;r++)output[p*CHANNELS+row+r]=F(a[r]);
}
void tiled_project(uint3 gid,uint t,uint matrix_offset,uint skip_offset,bool raw){
 if(gid.x*32>=CHANNELS||gid.y*8>=width*height)return;
 uint local_pixel=t/8,local_row=(t%8)*4,p=gid.y*8+local_pixel,row=gid.x*32+local_row;
 float4 a;[unroll]for(uint r=0;r<4;r++)a[r]=H(feature[p*CHANNELS+row+r]*weights[skip_offset+row+r]);
 [loop]for(uint g=0;g<HEADS;g++){
  [loop]for(uint i=t;i<8*32;i+=64)ffn_tile_input[(i/32)*33+i%32]=input[(gid.y*8+i/32)*CHANNELS+g*32+i%32];
  [loop]for(uint i=t;i<32*32;i+=64)ffn_tile_weight[(i%32)*33+i/32]=weights[matrix_offset+(gid.x*32+i/32)*CHANNELS+g*32+i%32];
  GroupMemoryBarrierWithGroupSync();float4 s=0;
  [loop]for(uint j=0;j<32;j++){float v=ffn_tile_input[local_pixel*33+j];uint w=j*33+local_row;s+=v*float4(ffn_tile_weight[w],ffn_tile_weight[w+1],ffn_tile_weight[w+2],ffn_tile_weight[w+3]);}
  a=float4(H(a.x+s.x),H(a.y+s.y),H(a.z+s.z),H(a.w+s.w));GroupMemoryBarrierWithGroupSync();
 }
 [unroll]for(uint r=0;r<4;r++)output[p*CHANNELS+row+r]=raw?a[r]:F(a[r]);
}
[numthreads(64,1,1)]void tiled_ffn_project(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){tiled_project(gid,tid.x,8*MATRIX,9*MATRIX,false);}
[numthreads(64,1,1)]void tiled_attention_project(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){tiled_project(gid,tid.x,3*MATRIX,SCALE_OFFSET+HEADS,RAW_OUTPUT!=0);}
[numthreads(64,1,1)]void tiled_split_project(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){tiled_project(gid,tid.x,0,MATRIX,false);}
[numthreads(64,1,1)]void split_ffn_project(uint3 id:SV_DispatchThreadID){
 uint n=id.x+id.y*4194240u;if(n>=width*height*CHANNELS)return;
 uint p=n/CHANNELS,row=n%CHANNELS;float a=H(feature[n]*weights[9*MATRIX+row]);
 [unroll]for(uint g=0;g<HEADS;g++){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[p*CHANNELS+g*32+j]*weights[8*MATRIX+row*CHANNELS+g*32+j];a=H(a+s);}
 output[n]=F(a);
}
#if NATIVE_PAD_MULTIHEAD_LDS && !NATIVE_WAVE_SCORES
#define ATTN_STRIDE 33
#else
#define ATTN_STRIDE 32
#endif
#if NATIVE_WAVE_SCORES
groupshared float16_t queries[2048],keys[2048];
#if NATIVE_WAVE_AV
groupshared float16_t values[2048];
groupshared float scores[4096];
#else
groupshared float values[2048],scores[4096];
#endif
#else
groupshared float queries[64*ATTN_STRIDE],keys[64*ATTN_STRIDE],values[64*ATTN_STRIDE];
#endif
#include "native_half_square.hlsli"
 #if NATIVE_WAVE_SCORES
[WaveSize(32)]
#endif
[numthreads(MULTIHEAD_THREADS,1,1)]void attention(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint head=gid.y,t=tid.x;if(gid.x>=width*height/64||head>=HEADS)return;
 uint p=((gid.x/(width/8))*8+t/8)*width+(gid.x%(width/8))*8+t%8;
 if(t<64){
 float q[32],k[32],qs[16],ks[16];
#if NATIVE_PRECOMPUTED_QKV
 [loop]for(uint c=0;c<32;c++){uint row=head*32+c;q[c]=feature[p*CHANNELS*3+row];k[c]=feature[p*CHANNELS*3+CHANNELS+row];values[t*ATTN_STRIDE+c]=F(feature[p*CHANNELS*3+2*CHANNELS+row]);}
#else
 [loop]for(uint c=0;c<32;c++){
  float a=0,b=0,z=0;uint row=head*32+c;
  [unroll]for(uint g=0;g<HEADS;g++){
   float sa=0,sb=0,sz=0;[loop]for(uint j=0;j<32;j++){float v=input[p*CHANNELS+g*32+j];sa+=v*weights[row*CHANNELS+g*32+j];sb+=v*weights[MATRIX+row*CHANNELS+g*32+j];sz+=v*weights[2*MATRIX+row*CHANNELS+g*32+j];}
   a=H(a+sa);b=H(b+sb);z=H(z+sz);
  }
  q[c]=a;k[c]=b;values[t*ATTN_STRIDE+c]=F(z);
 }
#endif
 [unroll]for(uint i=0;i<16;i++){qs[i]=NativeHalfSquarePair(q[i],q[i+16]);ks[i]=NativeHalfSquarePair(k[i],k[i+16]);}
 [unroll]for(uint i=0;i<8;i++){qs[i]=H(qs[i*2]+qs[i*2+1]);ks[i]=H(ks[i*2]+ks[i*2+1]);}
 [unroll]for(uint step=4;step>0;step/=2){[loop]for(uint i=0;i<step;i++){qs[i]=H(qs[i]+qs[i+step]);ks[i]=H(ks[i]+ks[i+step]);}}
 float qi=H(rsqrt(max(qs[0],6.198883056640625e-5))),ki=H(rsqrt(max(ks[0],6.198883056640625e-5)));
 [loop]for(uint c=0;c<32;c++){queries[t*ATTN_STRIDE+c]=F(H(H(q[c]*qi)*H(weights[SCALE_OFFSET+head])));keys[t*ATTN_STRIDE+c]=F(H(k[c]*ki));}
 }
 GroupMemoryBarrierWithGroupSync();
#if NATIVE_WAVE_SCORES
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 for(uint qr=MULTIHEAD_QUERY;qr<4;qr+=MULTIHEAD_QSTEP)for(uint kr=MULTIHEAD_COL_START;kr<4;kr+=MULTIHEAD_COL_STEP){
  A qa=A::Load(queries,qr*16*32,32,dx::linalg::MatrixLayout::RowMajor);
  B kb=B::Load(keys,kr*16*32,32,dx::linalg::MatrixLayout::ColMajor);
  auto s=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(qa,kb);
  s.Store(scores,qr*16*64+kr*16,64,dx::linalg::MatrixLayout::RowMajor);
 }
 GroupMemoryBarrierWithGroupSync();
#endif
#if NATIVE_PARALLEL_MULTIHEAD_SOFTMAX
 for(uint i=t;i<4096;i+=MULTIHEAD_THREADS){
  float score=H(scores[i]+weights[4*MATRIX+head*4096+i]);
  uint bits=f32tof16(clamp(H(score*.044921875+1.30078125),1.03125,1.5693359375));
  scores[i]=f16tof32(((bits<<5)+0x8000u)&65535u);
 }
 GroupMemoryBarrierWithGroupSync();
#endif
 if(t<64){
#if (NATIVE_SHARED_PROB && NATIVE_WAVE_AV) || NATIVE_PARALLEL_MULTIHEAD_SOFTMAX
 #define EXP_AT(x) scores[t*64+(x)]
#else
 float ex[64],prob[64];
 #define EXP_AT(x) ex[x]
#endif
#if !NATIVE_PARALLEL_MULTIHEAD_SOFTMAX
 [loop]for(uint key=0;key<64;key++){
#if NATIVE_WAVE_SCORES
  float s=scores[t*64+key];
#else
  float s=0;[loop]for(uint c=0;c<32;c++)s+=queries[t*ATTN_STRIDE+c]*keys[key*ATTN_STRIDE+c];
#endif
  float score=H(s+weights[4*MATRIX+head*4096+t*64+key]);uint bits=f32tof16(clamp(H(score*.044921875+1.30078125),1.03125,1.5693359375));EXP_AT(key)=f16tof32(((bits<<5)+0x8000u)&65535u);
 }
#endif
 float parity[2];[unroll]for(uint odd=0;odd<2;odd++){
  float total=0;[unroll]for(uint lane=0;lane<4;lane++){
   uint base=odd+(lane%2)*2+(lane/2)*8;float partial=H(EXP_AT(base)+EXP_AT(base+16));
   partial=H(partial+H(EXP_AT(base+4)+EXP_AT(base+20)));partial=H(partial+H(EXP_AT(base+32)+EXP_AT(base+48)));partial=H(partial+H(EXP_AT(base+36)+EXP_AT(base+52)));total=lane==0?partial:H(total+partial);
  }parity[odd]=total;
 }
 float inv=H(1/H(parity[0]+parity[1]));
#if NATIVE_PARALLEL_MULTIHEAD_SOFTMAX
 softmax_inverse[t]=inv;
#endif
#if !(NATIVE_SHARED_PROB && NATIVE_WAVE_AV) && !NATIVE_PARALLEL_MULTIHEAD_SOFTMAX
 [loop]for(uint key=0;key<64;key++)prob[key]=F(H(EXP_AT(key)*inv));
#endif
#if NATIVE_WAVE_AV
 // Q/K are dead after scores; reuse their rows for the two K32 probability blocks.
#if NATIVE_PARALLEL_MULTIHEAD_SOFTMAX
 // Probability conversion is assigned to the complete group below.
#elif NATIVE_SHARED_PROB
 for(uint c=0;c<32;c++){queries[t*32+c]=float16_t(F(H(EXP_AT(c)*inv)));keys[t*32+c]=float16_t(F(H(EXP_AT(32+c)*inv)));}
#else
 for(uint c=0;c<32;c++){queries[t*32+c]=float16_t(prob[c]);keys[t*32+c]=float16_t(prob[32+c]);}
#endif
#undef EXP_AT
 }
 GroupMemoryBarrierWithGroupSync();
#if NATIVE_PARALLEL_MULTIHEAD_SOFTMAX
 for(uint i=t;i<4096;i+=MULTIHEAD_THREADS){
  uint query=i/64,key=i%64;
  float prob=F(H(scores[i]*softmax_inverse[query]));
  if(key<32)queries[query*32+key]=float16_t(prob);
  else keys[query*32+key-32]=float16_t(prob);
 }
 GroupMemoryBarrierWithGroupSync();
#endif
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 for(uint qr=MULTIHEAD_QUERY;qr<4;qr+=MULTIHEAD_QSTEP)for(uint col=MULTIHEAD_COL_START*16;col<32;col+=16*MULTIHEAD_COL_STEP){
  A a=A::Load(queries,qr*16*32,32,dx::linalg::MatrixLayout::RowMajor);
  B b=B::Load(values,col,32,dx::linalg::MatrixLayout::RowMajor);
  C acc=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
  for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)));
  a=A::Load(keys,qr*16*32,32,dx::linalg::MatrixLayout::RowMajor);
  b=B::Load(values,32*32+col,32,dx::linalg::MatrixLayout::RowMajor);
  C next=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
  for(uint i=0;i<acc.Length();i++){
   uint2 coord=acc.GetCoordinate(i);uint query=qr*16+coord.x;
   uint pixel=((gid.x/(width/8))*8+query/8)*width+(gid.x%(width/8))*8+query%8;
   output[pixel*CHANNELS+head*32+col+coord.y]=F(H(acc.Get(i)+next.Get(i)));
  }
 }
#else
 [loop]for(uint c=0;c<32;c++){float a=0;[unroll]for(uint g=0;g<2;g++){float s=0;[loop]for(uint key=0;key<32;key++)s+=prob[g*32+key]*values[(g*32+key)*ATTN_STRIDE+c];a=H(a+s);}output[p*CHANNELS+head*32+c]=F(a);}
 }
#endif
}
[numthreads(64,1,1)]void projection(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=width*height)return;
 [loop]for(uint row=0;row<CHANNELS;row++){float a=H(feature[p*CHANNELS+row]*weights[SCALE_OFFSET+HEADS+row]);[unroll]for(uint g=0;g<HEADS;g++){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[p*CHANNELS+g*32+j]*weights[3*MATRIX+row*CHANNELS+g*32+j];a=H(a+s);}output[p*CHANNELS+row]=RAW_OUTPUT?a:F(a);}
}
