#include <dx/linalg.h>
#ifndef FULL_EXTENT
#define PIXELS 256
#define ROWS 32
#else
#define PIXELS 8640
#define ROWS 1024
#endif
ByteAddressBuffer weights:register(t0);
RWStructuredBuffer<float> output:register(u0);
float HSoftware(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float H(float v){
#if NATIVE_HALF
 return float(float16_t(v));
#else
 return HSoftware(v);
#endif
}
float load_half(uint pos){uint b=weights.Load(pos&~3u);return f16tof32((b>>((pos&2)*8))&65535);}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
float activate(float a){float g=clamp(a,-4.0,4.0),poly=H(g*H(abs(g)*(-.055908203125)+.447265625)+.89453125);return F(H(a*poly));}
[numthreads(32,1,1)]void main(uint3 id:SV_DispatchThreadID){
 if(id.x>=PIXELS)return;
 using Mat=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Thread>;
 vector<float,32>reference=0,actual=0;
 [loop]for(uint g=0;g<8;g++){
#if MATRIX_PATH != 2
  Mat m=Mat::Load<dx::linalg::MatrixLayout::RowMajor>(weights,(id.y*8+g)*2048,64);
#endif
  vector<float16_t,32>x;
  [unroll]for(uint j=0;j<32;j++)x[j]=float16_t(load_half(ROWS*256*2+(id.x*256+g*32+j)*2));
#if MATRIX_PATH != 2
  vector<float,32>y=dx::linalg::Multiply<float>(m,x);
#endif
  [unroll]for(uint row=0;row<32;row++){
#if MATRIX_PATH != 1
   float sum=0;[loop]for(uint j=0;j<32;j++)sum+=float(x[j])*load_half((id.y*8+g)*2048+(row*32+j)*2);
   reference[row]=H(reference[row]+sum);
#endif
#if MATRIX_PATH != 2
   actual[row]=H(actual[row]+y[row]);
#endif
  }
 }
 [unroll]for(uint row=0;row<32;row++){
#if APPLY_ACTIVATION
#if MATRIX_PATH != 2
  actual[row]=activate(actual[row]);
#endif
#if MATRIX_PATH != 1
  reference[row]=activate(reference[row]);
#endif
#endif
#if MATRIX_PATH == 1
  output[id.x*ROWS+id.y*32+row]=actual[row];
#elif MATRIX_PATH == 2
  output[id.x*ROWS+id.y*32+row]=reference[row];
#else
  output[id.x*ROWS+id.y*32+row]=reference[row]==actual[row]?0:1;
#endif
 }
}
