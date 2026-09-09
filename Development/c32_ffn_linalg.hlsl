#include <dx/linalg.h>
#ifndef WIDTH
#define WIDTH 960
#endif
#ifndef HEIGHT
#define HEIGHT 544
#endif
ByteAddressBuffer weights:register(t0);
StructuredBuffer<float> input:register(t1);
ByteAddressBuffer matrices:register(t2);
RWStructuredBuffer<float> output:register(u0);
float W(uint i){return asfloat(weights.Load(i*4));}
float F(float x){if(x==0)return 0;float s=x<0?-1:1,a=abs(x);if(a<.015625)return s*round(a*512)/512;float e=clamp(floor(log2(a)),-6.,8.),m=round((a/exp2(e)-1)*8);if(m>=8){m=0;e++;}return s*min(exp2(e)*(1+m/8),448.);}
[numthreads(64,1,1)]
void main(uint3 g:SV_GroupID,uint3 lane:SV_GroupThreadID){
    uint t=(g.y*65535+g.x)*64+lane.x;if(t>=WIDTH*HEIGHT)return;
    using E=dx::linalg::Matrix<dx::linalg::ComponentType::F16,64,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Thread>;
    using P=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,64,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Thread>;
    E expand=E::Load<dx::linalg::MatrixLayout::RowMajor>(matrices,0,64);
    P project=P::Load<dx::linalg::MatrixLayout::RowMajor>(matrices,4096,128);
    vector<float16_t,32> x;
    [unroll]for(uint c=0;c<32;c++)x[c]=float16_t(input[t*32+c]);
    vector<float,64> a=dx::linalg::Multiply<float>(expand,x);
    vector<float16_t,64> hidden;
    [unroll]for(uint j=0;j<64;j++){float v=clamp(a[j],-4.0,4.0);hidden[j]=float16_t(v*(0.89453125+v*(0.447265625-0.055908203125*abs(v))));}
    vector<float,32> y=dx::linalg::Multiply<float>(project,hidden);
    [unroll]for(uint c=0;c<32;c++)output[t*32+c]=F(y[c]+input[t*32+c]*W(10241+c));
}
