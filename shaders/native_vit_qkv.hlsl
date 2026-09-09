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
groupshared float tile_input[8*33],tile_weights[32*33];
[numthreads(64,1,1)]void project_tiled(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID){
 uint t=tid.x,part=gid.z,token=gid.y*8+t/8,row=gid.x*32+(t%8)*4;
 if(gid.y*8>=tokens)return;float4 total=0;
 [unroll]for(uint group=0;group<2;group++){
  float4 a=0;
  [loop]for(uint k=group*512;k<(group+1)*512;k+=32){
   for(uint i=t;i<256;i+=64)tile_input[(i/32)*33+i%32]=input[(gid.y*8+i/32)*1024+k+i%32];
   for(uint i=t;i<1024;i+=64)tile_weights[(i%32)*33+i/32]=weights[part*1048576+(gid.x*32+i/32)*1024+k+i%32];
   GroupMemoryBarrierWithGroupSync();float4 s=0;
   [loop]for(uint j=0;j<32;j++){uint w=j*33+(t%8)*4;float v=tile_input[(t/8)*33+j];s+=v*float4(tile_weights[w],tile_weights[w+1],tile_weights[w+2],tile_weights[w+3]);}
   a=float4(H(a.x+s.x),H(a.y+s.y),H(a.z+s.z),H(a.w+s.w));GroupMemoryBarrierWithGroupSync();
  }
  total=group?float4(H(total.x+a.x),H(total.y+a.y),H(total.z+a.z),H(total.w+a.w)):a;
 }
 [unroll]for(uint r=0;r<4;r++)output[(part*tokens+token)*1024+row+r]=total[r];
}
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
