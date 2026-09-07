#include <dx/linalg.h>
#ifndef INPUT_CHANNELS
#define INPUT_CHANNELS 4096
#endif
StructuredBuffer<float> input:register(t0),residual:register(t2);
ByteAddressBuffer weights:register(t1);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint tokens;uint output_base;}
groupshared float16_t tile[512];
groupshared float initial[256];
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=output_base/1024+gid.x*16;if(first>=tokens)return;
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 for(uint i=tid.x;i<256;i+=32){uint row=gid.y*16+i%16;initial[i]=H(residual[(first+i/16)*1024+row]*asfloat(weights.Load(INPUT_CHANNELS*1024*2+row*4)));}
 GroupMemoryBarrierWithGroupSync();C total=C::Splat(0.0f);
 [loop]for(uint part=0;part<4;part++){
  C acc=C::Splat(0.0f);if(part==0)acc=C::Load(initial,0,16,dx::linalg::MatrixLayout::RowMajor);
  [loop]for(uint k=part*(INPUT_CHANNELS/4);k<(part+1)*(INPUT_CHANNELS/4);k+=32){
   for(uint i=tid.x;i<512;i+=32)tile[i]=float16_t(input[(first+i/32)*INPUT_CHANNELS+k+i%32]);
   GroupMemoryBarrierWithGroupSync();
   A a=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
   B b=B::Load(weights,(gid.y*16*INPUT_CHANNELS+k)*2,INPUT_CHANNELS*2,dx::linalg::MatrixLayout::ColMajor,16);
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)+p.Get(i)));
   GroupMemoryBarrierWithGroupSync();
  }
  if(part==0)total=acc;else for(uint i=0;i<total.Length();i++)total.Set(i,H(total.Get(i)+acc.Get(i)));
 }
 for(uint i=0;i<total.Length();i++)total.Set(i,F(total.Get(i)));
 total.Store(output,(first*1024+gid.y*16)*4,1024*4,dx::linalg::MatrixLayout::RowMajor,16);
}
