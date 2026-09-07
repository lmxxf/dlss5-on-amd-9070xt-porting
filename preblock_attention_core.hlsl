// Original-layout 32-wide attention, lab 8x8 windows. Not yet live integration.
StructuredBuffer<float> weights:register(t0);
StructuredBuffer<float> input:register(t1);
RWStructuredBuffer<float> output:register(u0);
groupshared float queries[2048],keys[2048],values[2048];
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
float half_add_preserving_midpoint(float a,float b){
 precise float sum=a+b;
 precise float virtual_b=sum-a;
 precise float error=(a-(sum-virtual_b))+(b-virtual_b);
 uint bits=asuint(sum),magnitude=bits&0x7fffffffu;
 // Preserve the residual when a normal-half midpoint was created by float32
 // rounding. Other cases retain the existing conversion, including subnormals.
 if(magnitude>=0x38800000u&&magnitude<0x47800000u&&(magnitude&0x1fffu)==0x1000u&&error!=0){
  bool increase_bits=(error>0)==(sum>0);
  sum=asfloat(increase_bits?bits+1:bits-1);
 }
 return H(sum);
}
float fast_exp(float x){float a=clamp(H(x*.044921875+1.30078125),1.03125,1.5693359375);uint b=f32tof16(a);return f16tof32(((b<<5)+0x8000u)&65535u);}
#include "native_half_square.hlsli"
[numthreads(64,1,1)]
void main(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 if(gid.x*64>=TOTAL_OUTPUTS/32)return;
 uint t=tid.x,p=gid.x*64+t;
 float q[32],k[32],v[32],qs[32],ks[32];
 [loop]for(uint c=0;c<32;c++){
  float a=0,b=0,z=0;
  [loop]for(uint j=0;j<32;j++){float f=F(input[p*32+j]);a+=f*weights[c*32+j];b+=f*weights[1024+c*32+j];z+=f*weights[2048+c*32+j];}
  q[c]=H(a);k[c]=H(b);v[c]=F(H(z));
 }
 [unroll]for(uint i=0;i<16;i++){qs[i]=NativeHalfSquarePair(q[i],q[i+16]);ks[i]=NativeHalfSquarePair(k[i],k[i+16]);}
 [unroll]for(uint i=0;i<8;i++){qs[i]=H(qs[i*2]+qs[i*2+1]);ks[i]=H(ks[i*2]+ks[i*2+1]);}
 [unroll]for(uint step=4;step>0;step/=2){[loop]for(uint i=0;i<step;i++){qs[i]=H(qs[i]+qs[i+step]);ks[i]=H(ks[i]+ks[i+step]);}}
 float qi=H(rsqrt(max(qs[0],6.198883056640625e-5))),ki=H(rsqrt(max(ks[0],6.198883056640625e-5)));
 [loop]for(uint c=0;c<32;c++){queries[t*32+c]=F(H(H(q[c]*qi)*H(weights[8192])));keys[t*32+c]=F(H(k[c]*ki));values[t*32+c]=v[c];}
 GroupMemoryBarrierWithGroupSync();
 float ex[64],prob[64];
 [loop]for(uint key=0;key<64;key++){
  float dot=0;[loop]for(uint c=0;c<32;c++)dot+=queries[t*32+c]*keys[key*32+c];
  ex[key]=fast_exp(H(dot+weights[4096+t*64+key]));
 }
 float parity[2];
 [unroll]for(uint odd=0;odd<2;odd++){
  float total=0;
  [unroll]for(uint lane=0;lane<4;lane++){
   uint base=odd+(lane%2)*2+(lane/2)*8;
   float partial=H(ex[base]+ex[base+16]);
   partial=H(partial+H(ex[base+4]+ex[base+20]));
   partial=H(partial+H(ex[base+32]+ex[base+48]));
   partial=H(partial+H(ex[base+36]+ex[base+52]));
   total=lane==0?partial:H(total+partial);
  }
  parity[odd]=total;
 }
 float inv=H(1/H(parity[0]+parity[1]));[loop]for(uint key=0;key<64;key++)prob[key]=F(H(ex[key]*inv));
 float av[32];
 [loop]for(uint c=0;c<32;c++){
  float a=0;[unroll]for(uint group=0;group<2;group++){float s=0;[loop]for(uint key=0;key<32;key++)s+=prob[group*32+key]*values[(group*32+key)*32+c];a=H(a+s);}av[c]=F(a);
 }
 [loop]for(uint c=0;c<32;c++){
  float a=0;[loop]for(uint j=0;j<32;j++)a+=av[j]*weights[3072+c*32+j];
  float result=half_add_preserving_midpoint(a,H(input[p*32+c]*weights[8193+c]));
  output[p*32+c]=RAW_OUTPUT?result:F(result);
 }
}
