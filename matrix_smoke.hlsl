#include <dx/linalg.h>
ByteAddressBuffer weights : register(t0);
RWStructuredBuffer<float> output : register(u0);
[numthreads(32,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(id.x>=256)return;
    using Mat = dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Thread>;
    Mat matrix=Mat::Load<dx::linalg::MatrixLayout::RowMajor>(weights,0,64);
    vector<float16_t,32> x;
    [unroll]for(uint i=0;i<32;i++)x[i]=float16_t(float((id.x%8+1)*(i+1))/32.0);
    vector<float16_t,32> y=dx::linalg::Multiply<float16_t>(matrix,x);
    [unroll]for(uint i=0;i<32;i++)output[id.x*32+i]=float(y[i]);
}
