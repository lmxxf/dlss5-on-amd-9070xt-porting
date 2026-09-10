#include <dx/linalg.h>
ByteAddressBuffer weights:register(t0);
StructuredBuffer<float> input:register(t1);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint seed;uint width;uint height;uint local_oracle;uint temporal;}
groupshared float16_t prefix[512],hidden[2048];
groupshared float temp[256];
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=(gid.x+gid.y*65535u)*16,t=tid.x;if(first>=width*height)return;
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 for(uint i=t;i<512;i+=32)prefix[i]=float16_t(F(input[first*32+i]));
 GroupMemoryBarrierWithGroupSync();
 for(uint block=0;block<8;block++){
  A a=A::Load(prefix,0,32,dx::linalg::MatrixLayout::RowMajor);
  B b=B::Load(weights,block*16*32*2,64,dx::linalg::MatrixLayout::ColMajor,16);
  C h=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
  for(uint i=0;i<h.Length();i++){float v=H(h.Get(i)),g=clamp(v,-4.0,4.0),p=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);h.Set(i,F(H(v*p)));}
  h.Store(temp,0,16,dx::linalg::MatrixLayout::RowMajor);GroupMemoryBarrierWithGroupSync();
  for(uint i=t;i<256;i+=32)hidden[(i/16)*128+block*16+i%16]=float16_t(temp[i]);
  GroupMemoryBarrierWithGroupSync();
 }
 GroupMemoryBarrierWithGroupSync();
 for(uint block=0;block<2;block++){
  for(uint i=t;i<256;i+=32){uint c=block*16+i%16;temp[i]=H(input[(first+i/16)*32+c]*asfloat(weights.Load(16384+c*4)));}
  GroupMemoryBarrierWithGroupSync();C acc=C::Load(temp,0,16,dx::linalg::MatrixLayout::RowMajor);
  for(uint g=0;g<4;g++){
   A a=A::Load(hidden,g*32,128,dx::linalg::MatrixLayout::RowMajor);
   B b=B::Load(weights,8192+(block*16*128+g*32)*2,256,dx::linalg::MatrixLayout::ColMajor,16);
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)+p.Get(i)));
  }
  acc.Store(temp,0,16,dx::linalg::MatrixLayout::RowMajor);GroupMemoryBarrierWithGroupSync();
  for(uint i=t;i<256;i+=32)output[(first+i/16)*32+block*16+i%16]=temp[i];
  GroupMemoryBarrierWithGroupSync();
 }
}
