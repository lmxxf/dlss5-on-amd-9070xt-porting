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
StructuredBuffer<float> input:register(t0),residual:register(t2);
ByteAddressBuffer weights:register(t1);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint tokens;uint output_base;}
groupshared float16_t tile[512];
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
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
    B b=B::Load(weights,(col*INPUT_CHANNELS+k)*2,INPUT_CHANNELS*2,dx::linalg::MatrixLayout::ColMajor,16);
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
 [unroll]for(uint n=0;n<BLOCK_N;n++){
  uint col=(gid.y*BLOCK_N+n)*16;
  for(uint i=0;i<total[n].Length();i++){
   uint2 rc=total[n].GetCoordinate(i);uint token=first+rc.x,row=col+rc.y;float t=total[n].Get(i);
   float scale=asfloat(weights.Load(INPUT_CHANNELS*OUTPUT_CHANNELS*2+row*4));
   [unroll]for(uint dy=0;dy<2;dy++)[unroll]for(uint dx=0;dx<2;dx++){
    uint x=token%width*2+dx,y=token/width*2+dy;
    if(x>=output_width||y>=output_height)continue;
    uint index=(y*output_width+x)*OUTPUT_CHANNELS+row;
    float merged=H(t+F(residual[index])*scale);
    output[index]=OUTPUT_CHANNELS==32?merged:F(merged);
   }
  }
 }
}
