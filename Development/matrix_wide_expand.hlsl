#include <dx/linalg.h>
ByteAddressBuffer weights:register(t0);
RWStructuredBuffer<float> output:register(u0);
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
float load_half(uint pos){uint b=weights.Load(pos&~3u);return f16tof32((b>>((pos&2)*8))&65535);}
[numthreads(32,1,1)]void main(uint3 id:SV_DispatchThreadID){
 if(id.x>=8640)return;
 using Mat=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,256,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Thread>;
 Mat m=Mat::Load<dx::linalg::MatrixLayout::RowMajor>(weights,id.y*32*256*2,512);
 vector<float16_t,256>x;[unroll]for(uint j=0;j<256;j++)x[j]=float16_t(load_half(1024*256*2+(id.x*256+j)*2));
 vector<float,32>y=dx::linalg::Multiply<float>(m,x);
 [unroll]for(uint row=0;row<32;row++){float a=H(y[row]),g=clamp(a,-4.0,4.0),poly=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);output[id.x*1024+id.y*32+row]=F(H(a*poly));}
}
