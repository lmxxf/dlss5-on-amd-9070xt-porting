#ifndef CHANNELS
#define CHANNELS 64
#endif
StructuredBuffer<float> raw:register(t0),weights:register(t1);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;uint source_width;uint unused_x;uint unused_y;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=width*height)return;uint base=((p/width)*2*source_width+(p%width)*2)*CHANNELS;
 float pooled[CHANNELS];[loop]for(uint c=0;c<CHANNELS;c++){
  float top=H(raw[base+c]+raw[base+CHANNELS+c]);
  float bottom=H(raw[base+source_width*CHANNELS+c]+raw[base+(source_width+1)*CHANNELS+c]);
  pooled[c]=F(H(H(top+bottom)*.25));
 }
 [loop]for(uint row=0;row<2*CHANNELS;row++){
  float a=0;[unroll]for(uint group=0;group<CHANNELS/32;group++){float s=0;[loop]for(uint j=0;j<32;j++)s+=pooled[group*32+j]*weights[row*CHANNELS+group*32+j];a=H(a+s);}
  output[p*(2*CHANNELS)+row]=F(a);
 }
}
