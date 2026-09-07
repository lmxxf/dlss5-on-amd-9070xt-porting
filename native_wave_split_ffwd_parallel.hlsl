#include <dx/linalg.h>
// Dispatch Y selects one of eight independent 64-channel FFN groups.
// Compute only that group's mix rows; all 512 input channels remain dependencies.
// The original full-group fused candidate remains in native_wave_split_ffwd.hlsl.
StructuredBuffer<float> input:register(t0);
ByteAddressBuffer weights:register(t1);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;}
groupshared float16_t mixed[16*80],hidden[16*272],tile[512];
groupshared float temp[256];
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint first=gid.x*16,t=tid.x;if(first>=width*height)return;
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 uint group=gid.y;
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
 {
  for(uint block=0;block<16;block++){
   C acc=C::Splat(0.0f);
   for(uint k=0;k<2;k++){
    A a=A::Load(mixed,k*32,80,dx::linalg::MatrixLayout::RowMajor);
    B b=B::Load(weights,(262144+group*16384+block*16*64+k*32)*2,128,dx::linalg::MatrixLayout::ColMajor,16);
    C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
    for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)+p.Get(i)));
   }
   for(uint i=0;i<acc.Length();i++){float v=acc.Get(i),g=clamp(v,-4.0,4.0),p=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);acc.Set(i,F(H(v*p)));}
   acc.Store(temp,0,16,dx::linalg::MatrixLayout::RowMajor);GroupMemoryBarrierWithGroupSync();
   for(uint i=t;i<256;i+=32)hidden[(i/16)*272+block*16+i%16]=float16_t(temp[i]);
   GroupMemoryBarrierWithGroupSync();
  }
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
  GroupMemoryBarrierWithGroupSync();
 }
}
