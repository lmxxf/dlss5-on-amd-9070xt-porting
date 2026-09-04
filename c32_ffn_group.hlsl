#ifndef WIDTH
#define WIDTH 960
#endif
#ifndef HEIGHT
#define HEIGHT 544
#endif
#ifndef TILED_INPUT
#define TILED_INPUT 0
#endif
ByteAddressBuffer w:register(t0);
StructuredBuffer<float> inp:register(t1),feature_unused:register(t2),qkv_unused:register(t3);
RWStructuredBuffer<float> feat:register(u0),output_unused:register(u1),qkv_output_unused:register(u2);
groupshared float x[32],hidden[64];
float W(uint i){return asfloat(w.Load(i*4));}
float F(float v){if(v==0)return 0;float s=v<0?-1:1,a=abs(v);if(a<.015625)return s*round(a*512)/512;float e=clamp(floor(log2(a)),-6.,8.),m=round((a/exp2(e)-1)*8);if(m>=8){m=0;e++;}return s*min(exp2(e)*(1+m/8),448.);}
uint I(uint t,uint c){if(!TILED_INPUT)return t*32+c;uint px=t%WIDTH,py=t/WIDTH;return ((py/8)*(WIDTH/8)+px/8)*2048+((py%8)*8+px%8)*32+c;}
[numthreads(64,1,1)]
void main(uint3 group:SV_GroupID,uint3 lane:SV_GroupThreadID){
    uint t=group.y*65535+group.x,j=lane.x;
    if(t>=WIDTH*HEIGHT)return;
    if(j<32)x[j]=inp[I(t,j)];
    GroupMemoryBarrierWithGroupSync();
    precise float a=0;
    [unroll]for(uint c=0;c<32;c++)a+=x[c]*W(j*32+c);
    a=clamp(a,-4.,4.);
    hidden[j]=a*(.89453125+a*(.447265625-.055908203125*abs(a)));
    GroupMemoryBarrierWithGroupSync();
    if(j<32){
        precise float v=0;
        [unroll]for(uint k=0;k<64;k++)v+=hidden[k]*W(2048+j*64+k);
        feat[t*32+j]=F(v+x[j]*W(10241+j));
    }
}
