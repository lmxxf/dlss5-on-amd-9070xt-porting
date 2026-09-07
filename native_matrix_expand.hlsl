#ifndef MATRIX_CHANNELS
#define MATRIX_CHANNELS 256
#endif
#include <dx/linalg.h>
#if PACKED_INPUT
ByteAddressBuffer input:register(t0);
#else
StructuredBuffer<float> input:register(t0);
#endif
ByteAddressBuffer weights:register(t1);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[numthreads(32,1,1)]void main(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=width*height)return;
 using Mat=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Thread>;
 vector<float,32>a=0;
 [loop]for(uint g=0;g<MATRIX_CHANNELS/32;g++){
  Mat m=Mat::Load<dx::linalg::MatrixLayout::RowMajor>(weights,(id.y*(MATRIX_CHANNELS/32)+g)*2048,64);
  vector<float16_t,32>x;[unroll]for(uint j=0;j<32;j++){
#if PACKED_INPUT
   uint pos=(p*MATRIX_CHANNELS+g*32+j)*2,bits=input.Load(pos&~3u);x[j]=float16_t(f16tof32((bits>>((pos&2)*8))&65535u));
#else
   x[j]=float16_t(input[p*MATRIX_CHANNELS+g*32+j]);
#endif
  }
  vector<float,32>y=dx::linalg::Multiply<float>(m,x);
  [unroll]for(uint r=0;r<32;r++)a[r]=H(a[r]+y[r]);
 }
 [unroll]for(uint r=0;r<32;r++){float g=clamp(a[r],-4.0,4.0),poly=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);output[p*(4*MATRIX_CHANNELS)+id.y*32+r]=F(H(a[r]*poly));}
}
