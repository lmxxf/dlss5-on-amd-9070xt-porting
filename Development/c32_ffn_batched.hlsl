#define WIDTH 960
#define HEIGHT 544
#define BATCH 4
#define LANES 16
ByteAddressBuffer w:register(t0);
StructuredBuffer<float> inp:register(t1),feature_unused:register(t2),qkv_unused:register(t3);
RWStructuredBuffer<float> feat:register(u0),output_unused:register(u1),qkv_output_unused:register(u2);
groupshared float x[BATCH*32],h[BATCH*64];
float W(uint i){return asfloat(w.Load(i*4));}
float F(float v){if(v==0)return 0;float s=v<0?-1:1,a=abs(v);if(a<.015625)return s*round(a*512)/512;float e=clamp(floor(log2(a)),-6.,8.),m=round((a/exp2(e)-1)*8);if(m>=8){m=0;e++;}return s*min(exp2(e)*(1+m/8),448.);}
[numthreads(64,1,1)]
void main(uint3 group:SV_GroupID,uint3 lane:SV_GroupThreadID){
    uint base=(group.y*65535+group.x)*BATCH;
    if(base>=WIDTH*HEIGHT)return;
    uint local=lane.x/LANES,l=lane.x%LANES,t=base+local;
    [unroll]for(uint part=0;part<2;part++){uint c=l+part*LANES;x[local*32+c]=inp[t*32+c];}
    GroupMemoryBarrierWithGroupSync();
    [unroll]for(uint part=0;part<4;part++){uint j=l+part*LANES;
        precise float a=0;[unroll]for(uint c=0;c<32;c++)a+=x[local*32+c]*W(j*32+c);
        a=clamp(a,-4.,4.);h[local*64+j]=a*(.89453125+a*(.447265625-.055908203125*abs(a)));
    }
    GroupMemoryBarrierWithGroupSync();
    [unroll]for(uint part=0;part<2;part++){uint c=l+part*LANES;
        precise float v=0;[unroll]for(uint j=0;j<64;j++)v+=h[local*64+j]*W(2048+c*64+j);
        feat[t*32+c]=F(v+x[local*32+c]*W(10241+c));
    }
}
