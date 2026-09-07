#include <dx/linalg.h>
ByteAddressBuffer weights:register(t0);
RWStructuredBuffer<float> output:register(u0);
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float load_half(uint pos){uint b=weights.Load(pos&~3u);return f16tof32((b>>((pos&2)*8))&65535);}
[numthreads(32,1,1)]void main(uint3 id:SV_DispatchThreadID){
 if(id.x>=256)return;
 using Mat=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Thread>;
 vector<float,32>reference=0,actual=0;
 [loop]for(uint g=0;g<8;g++){
#if MATRIX_PATH != 2
  Mat m=Mat::Load<dx::linalg::MatrixLayout::RowMajor>(weights,g*2048,64);
#endif
  vector<float16_t,32>x;
  [unroll]for(uint j=0;j<32;j++)x[j]=float16_t(load_half(16384+(id.x*256+g*32+j)*2));
#if MATRIX_PATH != 2
  vector<float,32>y=dx::linalg::Multiply<float>(m,x);
#endif
  [unroll]for(uint row=0;row<32;row++){
#if MATRIX_PATH != 1
   float sum=0;[loop]for(uint j=0;j<32;j++)sum+=float(x[j])*load_half(g*2048+(row*32+j)*2);
   reference[row]=H(reference[row]+sum);
#endif
#if MATRIX_PATH != 2
   actual[row]=H(actual[row]+y[row]);
#endif
  }
 }
 [unroll]for(uint row=0;row<32;row++){
#if MATRIX_PATH == 1
  output[id.x*32+row]=actual[row];
#elif MATRIX_PATH == 2
  output[id.x*32+row]=reference[row];
#else
  output[id.x*32+row]=reference[row]==actual[row]?0:1;
#endif
 }
}
