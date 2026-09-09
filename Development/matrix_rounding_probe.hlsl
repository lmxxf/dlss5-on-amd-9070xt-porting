#include <dx/linalg.h>
ByteAddressBuffer weights:register(t0);
RWStructuredBuffer<float> output:register(u0);
uint hash(uint x){x^=x>>16;x*=0x7feb352d;x^=x>>15;x*=0x846ca68b;return x^(x>>16);}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
[numthreads(32,1,1)]void main(uint3 id:SV_DispatchThreadID){
 if(id.x>=256)return;
 using Mat=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Thread>;
 Mat m=Mat::Load<dx::linalg::MatrixLayout::RowMajor>(weights,0,64);
 vector<float16_t,32>x;
 [unroll]for(uint i=0;i<32;i++){uint h=hash(id.x*32+i+297);x[i]=float16_t(f16tof32(((h>>16)&0x8000)|((9+(h%10))<<10)|(((h>>8)&7)<<7)));}
 vector<float,32>y=dx::linalg::Multiply<float>(m,x);
 [unroll]for(uint row=0;row<32;row++){
  float sum=0;[loop]for(uint j=0;j<32;j++){uint pos=(row*32+j)*2,b=weights.Load(pos&~3);float w=f16tof32((b>>((pos&2)*8))&65535);sum+=float(x[j])*w;}
  float initial=float(int(hash(id.x+row)&255)-128)/16;
  float a=H(sum+initial),b=H(y[row]+initial);
  output[id.x*32+row]=a==b?0:1;
 }
}
