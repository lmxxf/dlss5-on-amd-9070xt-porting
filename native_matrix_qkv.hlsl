#include <dx/linalg.h>
ByteAddressBuffer input:register(t0),weights:register(t1);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
[numthreads(32,1,1)]void main(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=width*height)return;
 using Mat=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Thread>;
 vector<float,32>a=0;
 [loop]for(uint g=0;g<8;g++){
  Mat m=Mat::Load<dx::linalg::MatrixLayout::RowMajor>(weights,((id.z*8+id.y)*8+g)*2048,64);
  vector<float16_t,32>x;[unroll]for(uint j=0;j<32;j++){uint pos=(p*256+g*32+j)*2,b=input.Load(pos&~3u);x[j]=float16_t(f16tof32((b>>((pos&2)*8))&65535u));}
  vector<float,32>y=dx::linalg::Multiply<float>(m,x);[unroll]for(uint r=0;r<32;r++)a[r]=H(a[r]+y[r]);
 }
 [unroll]for(uint r=0;r<32;r++)output[(p*3+id.z)*256+id.y*32+r]=a[r];
}
