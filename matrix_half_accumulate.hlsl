#include <dx/linalg.h>
ByteAddressBuffer weights:register(t0);
RWByteAddressBuffer output:register(u0);
groupshared float16_t result_tile[256];
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 C acc=C::Splat(float16_t(0));
 [loop]for(uint g=0;g<8;g++){
  A a=A::Load(weights,524288+(gid.x*16*256+g*32)*2,512,dx::linalg::MatrixLayout::RowMajor,16);
  B b=B::Load(weights,(gid.y*16*256+g*32)*2,512,dx::linalg::MatrixLayout::ColMajor,16);
  acc.MultiplyAccumulate(a,b);
 }
 for(uint i=0;i<acc.Length();i++){float a=acc.Get(i),g=clamp(a,-4.0,4.0),p=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);acc.Set(i,F(H(a*p)));}
 acc.Store(result_tile,0,16,dx::linalg::MatrixLayout::RowMajor);
 GroupMemoryBarrierWithGroupSync();
 for(uint i=tid.x;i<256;i+=32)output.Store(((gid.x*16+i/16)*1024+gid.y*16+i%16)*4,asuint(float(result_tile[i])));
}
