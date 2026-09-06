StructuredBuffer<float> input:register(t0),weights:register(t1),residual:register(t2);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[numthreads(64,1,1)]void ffwd(uint3 id:SV_DispatchThreadID){
 uint p=id.x;[branch]if(p>=width*height)return;float mixed[512];
 [loop]for(uint row=0;row<512;row++){
  float a=0;[loop]for(uint k=0;k<16;k++){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[p*512+k*32+j]*weights[row*512+k*32+j];a=H(a+s);}mixed[row]=F(a);
 }
 [loop]for(uint group=0;group<8;group++){
  float hidden[256];
  [loop]for(uint row=0;row<256;row++){
   float a=0;[unroll]for(uint k=0;k<2;k++){float s=0;[loop]for(uint j=0;j<32;j++)s+=mixed[group*64+k*32+j]*weights[262144+group*16384+row*64+k*32+j];a=H(a+s);}
   float gate=clamp(a,-4.0,4.0),poly=H(gate*H(abs(gate)*(-.055908203125)+.447265625)+.89453125);hidden[row]=F(H(a*poly));
  }
  [loop]for(uint row=0;row<64;row++){
   float a=0;[unroll]for(uint k=0;k<8;k++){float s=0;[loop]for(uint j=0;j<32;j++)s+=hidden[k*32+j]*weights[393216+group*16384+row*256+k*32+j];a=H(a+s);}output[p*512+group*64+row]=F(a);
  }
 }
}
[numthreads(64,1,1)]void ffwd_projection(uint3 id:SV_DispatchThreadID){
 uint p=id.x;[branch]if(p>=width*height)return;
 [loop]for(uint row=0;row<512;row++){
  float a=H(residual[p*512+row]*weights[262144+row]);
  [loop]for(uint k=0;k<16;k++){float s=0;[loop]for(uint j=0;j<32;j++)s+=input[p*512+k*32+j]*weights[row*512+k*32+j];a=H(a+s);}
  output[p*512+row]=F(a);
 }
}
