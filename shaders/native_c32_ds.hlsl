StructuredBuffer<float> pooled:register(t0);
StructuredBuffer<float> weights:register(t1);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;uint work_width;uint crop_x;uint crop_y;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=width*height)return;
 uint base=((p/width+crop_y)*work_width+p%width+crop_x)*32;
 [loop]for(uint row=0;row<64;row++){
  float sum=0;[loop]for(uint c=0;c<32;c++)sum+=pooled[base+c]*weights[row*32+c];
  output[p*64+row]=F(H(sum));
 }
}
