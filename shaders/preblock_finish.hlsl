StructuredBuffer<float> raw:register(t0);
RWStructuredBuffer<float> main_output:register(u0),down_output:register(u1);
RWStructuredBuffer<uint> main8_output:register(u0); // FAST PATH (DLSS5_PREBLOCK_MAIN8): E4M3 main, 4 channels per uint
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
[numthreads(64,1,1)]void finish_coalesced(uint3 group:SV_GroupID,uint t:SV_GroupIndex){
 uint n=(group.y*65535+group.x)*64+t;
 if(n>=width*height*32)return;
 uint p=n/32,c=n%32,x=p%width,y=p/width;
 main_output[n]=F(raw[index(x,y,c)]);
 if((x%2)==0&&(y%2)==0){
  float top=H(raw[index(x,y,c)]+raw[index(x+1,y,c)]);
  float bottom=H(raw[index(x,y+1,c)]+raw[index(x+1,y+1,c)]);
  down_output[((y/2)*(width/2)+x/2)*32+c]=F(H(H(top+bottom)*.25));
 }
}
// FAST PATH (DLSS5_PREBLOCK_DOWN_ONLY): only the 2x2 pooled buffer; one thread per pooled element.
[numthreads(64,1,1)]void finish_down(uint3 group:SV_GroupID,uint t:SV_GroupIndex){
 uint n=(group.y*65535+group.x)*64+t;
 if(n>=(width/2)*(height/2)*32)return;
 uint q=n/32,c=n%32,x=(q%(width/2))*2,y=(q/(width/2))*2;
 float top=H(raw[index(x,y,c)]+raw[index(x+1,y,c)]);
 float bottom=H(raw[index(x,y+1,c)]+raw[index(x+1,y+1,c)]);
 down_output[n]=F(H(H(top+bottom)*.25));
}
uint E4M3(float v){uint b=asuint(v),a=b&0x7fffffffu,sg=(b>>24)&0x80u;if(a==0)return sg;float m=abs(v);if(m<0.015625)return sg|uint(round(m*512.0));uint e=(a>>23)-127+7,mant=(a>>20)&7u;if(e>15)return sg|0x7eu;return sg|(e<<3)|mant;}
// FAST PATH (DLSS5_PREBLOCK_MAIN8): main as E4M3 bytes (values are F() outputs, so exact); one thread per pixel x 4 channels.
[numthreads(64,1,1)]void finish_main8(uint3 group:SV_GroupID,uint t:SV_GroupIndex){
 uint n=(group.y*65535+group.x)*64+t;
 if(n>=width*height*8)return;
 uint p=n/8,c0=(n%8)*4,x=p%width,y=p/width;
 uint packed=0;
 [unroll]for(uint k=0;k<4;k++){uint c=c0+k;float v=F(raw[index(x,y,c)]);packed|=E4M3(v)<<(k*8);
  if((x%2)==0&&(y%2)==0){float top=H(raw[index(x,y,c)]+raw[index(x+1,y,c)]);float bottom=H(raw[index(x,y+1,c)]+raw[index(x+1,y+1,c)]);down_output[((y/2)*(width/2)+x/2)*32+c]=F(H(H(top+bottom)*.25));}}
 main8_output[n]=packed;
}
