#ifndef MATRIX_CHANNELS
#define MATRIX_CHANNELS 512
#endif
StructuredBuffer<float> input:register(t0);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;uint source_width;uint valid_width;uint valid_height;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u)return (sg?-1:1)*round(abs(v)*16777216.0)*5.9604644775390625e-8;uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){
 uint n=id.x;if(n>=width*height*(MATRIX_CHANNELS/2))return;uint p=n/(MATRIX_CHANNELS/2),c=(n%(MATRIX_CHANNELS/2))*2,x=p%width,y=p/width;
 uint packed=0;
 if(valid_width==0||(x<valid_width&&y<valid_height)){
  uint base=(y*2*source_width+x*2)*MATRIX_CHANNELS;
  for(uint j=0;j<2;j++){float top=H(input[base+c+j]+input[base+MATRIX_CHANNELS+c+j]),bottom=H(input[base+source_width*MATRIX_CHANNELS+c+j]+input[base+(source_width+1)*MATRIX_CHANNELS+c+j]);float v=F(H(H(top+bottom)*.25));packed|=(f32tof16(v)&65535u)<<(j*16);}
 }
 output.Store(n*4,packed);
}
