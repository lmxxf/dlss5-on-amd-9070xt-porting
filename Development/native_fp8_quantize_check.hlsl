#include "native_fp8_fast.hlsli"
RWStructuredBuffer<uint4> output:register(u0);
float Legacy(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){
 uint h=id.x;if(h>=65536)return;
 if((h&0x7c00)==0x7c00){output[h]=uint4(0,0,h,0);return;}
 float v=f16tof32(h);output[h]=uint4(asuint(Legacy(v)),asuint(NativeFastFp8(v)),h,1);
}
