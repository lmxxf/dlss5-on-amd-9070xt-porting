StructuredBuffer<float> raw:register(t0);
RWStructuredBuffer<float> main_output:register(u0),down_output:register(u1);
cbuffer RuntimeParameters:register(b0){uint seed;uint width;uint height;uint local_oracle;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
uint index(uint x,uint y,uint c){uint tile=(y/8)*(width/8)+x/8;return (tile*64+(y%8)*8+x%8)*32+c;}
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=width*height)return;uint x=p%width,y=p/width;
 [loop]for(uint c=0;c<32;c++){
  main_output[p*32+c]=F(raw[index(x,y,c)]);
  if((x%2)==0&&(y%2)==0){float top=H(raw[index(x,y,c)]+raw[index(x+1,y,c)]),bottom=H(raw[index(x,y+1,c)]+raw[index(x+1,y+1,c)]);down_output[((y/2)*(width/2)+x/2)*32+c]=F(H(H(top+bottom)*.25));}
 }
}
