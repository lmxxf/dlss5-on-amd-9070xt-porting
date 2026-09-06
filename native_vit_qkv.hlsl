StructuredBuffer<float> input:register(t0),weights:register(t1);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint tokens;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[numthreads(64,1,1)]void project(uint3 id:SV_DispatchThreadID){
 if(id.x>=tokens*3072)return;uint part=id.x/(tokens*1024),token=(id.x/1024)%tokens,row=id.x%1024;float total=0;
 [unroll]for(uint group=0;group<2;group++){
  float a=0;[loop]for(uint k=group*512;k<(group+1)*512;k+=32){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[token*1024+k+j]*weights[part*1048576+row*1024+k+j];a=H(a+s);}
  total=group?H(total+a):a;
 }
 output[id.x]=total;
}
uint tensor_channel(uint c){return ((c&1)<<1)|((c&2)>>1)|((c&4)<<2)|((c&8)>>1)|((c&16)>>1);}
#include "native_half_square.hlsli"
[numthreads(64,1,1)]void normalize(uint3 id:SV_DispatchThreadID){
 if(id.x>=tokens*96)return;uint part=id.x/(tokens*32),token=(id.x/32)%tokens,head=id.x%32,base=(part*tokens+token)*1024+head*32;
 if(part==2){[unroll]for(uint c=0;c<32;c++)output[base+c]=F(input[base+c]);return;}
 float v[32],s[16];[unroll]for(uint c=0;c<32;c++)v[tensor_channel(c)]=input[base+c];
 [unroll]for(uint i=0;i<16;i++)s[i]=NativeHalfSquarePair(v[i],v[i+16]);
 [unroll]for(uint i=0;i<8;i++)s[i]=H(s[i*2]+s[i*2+1]);
 [unroll]for(uint width=4;width>0;width/=2){[loop]for(uint i=0;i<width;i++)s[i]=H(s[i]+s[i+width]);}
 float inverse=H(rsqrt(max(s[0],6.198883056640625e-5)));
 [unroll]for(uint c=0;c<32;c++){float a=H(input[base+c]*inverse);if(part==0)a=H(H(a*5.65625)*H(weights[3145728+head]));output[base+c]=F(a);}
}
