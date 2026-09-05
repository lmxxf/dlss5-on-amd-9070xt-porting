#ifndef WIDTH
#define WIDTH 1920
#endif
#ifndef HEIGHT
#define HEIGHT 1088
#endif
#ifndef SHIFTED
#define SHIFTED 0
#endif
#ifndef WINDOW_SET
#define WINDOW_SET 0
#endif
ByteAddressBuffer w:register(t0);
StructuredBuffer<float> inp:register(t1),feat_in:register(t2),qkv_in:register(t3);
RWStructuredBuffer<float> feat:register(u0),outp:register(u1),qkv_out:register(u2);
groupshared float keys[1024],values[1024],key_norm[64];
#if CACHE_SCORES == 1
groupshared float scores[4096];
#endif
float W(uint i){return asfloat(w.Load(i*4));}
uint region(uint p,uint n){return p<n-8?0:(p<n-4?1:2);}
float F(float x){if(x==0)return 0;float s=x<0?-1:1,a=abs(x);if(a<.015625)return s*round(a*512)/512;float e=clamp(floor(log2(a)),-6.,8.),m=round((a/exp2(e)-1)*8);if(m>=8){m=0;e++;}return s*min(exp2(e)*(1+m/8),448.);}
[numthreads(64,1,1)]
void main(uint3 group:SV_GroupID,uint3 lane:SV_GroupThreadID){
    uint window=group.x+group.y*65535,query=lane.x;
#if WINDOW_SET == 1
    if(window>=(WIDTH/8-1)*(HEIGHT/8-1))return;
    window=(window/(WIDTH/8-1))*(WIDTH/8)+window%(WIDTH/8-1);
#elif WINDOW_SET == 2
    if(window>=WIDTH/8+HEIGHT/8-1)return;
    window=window<HEIGHT/8-1?window*(WIDTH/8)+WIDTH/8-1:(HEIGHT/8-1)*(WIDTH/8)+window-(HEIGHT/8-1);
#endif
    if(window>=(WIDTH/8)*(HEIGHT/8))return;
    uint wx=(window%(WIDTH/8))*8,wy=(window/(WIDTH/8))*8;
    uint rx=wx+query%8,ry=wy+query/8;
    uint ox=SHIFTED?(rx+4)%WIDTH:rx,oy=SHIFTED?(ry+4)%HEIGHT:ry;
    uint t=oy*WIDTH+ox;
    float q[16],qq=0,kk=0;
    [unroll]for(uint d=0;d<16;d++){
        q[d]=qkv_in[t*48+d];qq+=q[d]*q[d];
        float k=qkv_in[t*48+16+d];kk+=k*k;
        keys[query*16+d]=k;values[query*16+d]=qkv_in[t*48+32+d];
    }
    key_norm[query]=rsqrt(max(kk,1e-12));
    GroupMemoryBarrierWithGroupSync();
    float qnorm=rsqrt(max(qq,1e-12)),scale=W(10240),mx=-3.4e38;
#if CACHE_SCORES == 2
    float local_scores[64];
    [unroll]
#else
    [loop]
#endif
    for(uint k=0;k<64;k++){
        bool allowed=WINDOW_SET==1||!SHIFTED||(region(rx,WIDTH)==region(wx+k%8,WIDTH)&&region(ry,HEIGHT)==region(wy+k/8,HEIGHT));
        float dot=0;[unroll]for(uint d=0;d<16;d++)dot+=q[d]*keys[k*16+d];
        float score=allowed?dot*qnorm*key_norm[k]*scale+W(6144+query*64+k):-3.4e38;
#if CACHE_SCORES == 1
        scores[k*64+query]=score;
#elif CACHE_SCORES == 2
        local_scores[k]=score;
#endif
        mx=max(mx,score);
    }
    float den=0,a[16];[unroll]for(uint d=0;d<16;d++)a[d]=0;
#if CACHE_SCORES == 2
    [unroll]
#else
    [loop]
#endif
    for(uint k=0;k<64;k++){
        bool allowed=WINDOW_SET==1||!SHIFTED||(region(rx,WIDTH)==region(wx+k%8,WIDTH)&&region(ry,HEIGHT)==region(wy+k/8,HEIGHT));
#if CACHE_SCORES == 1
        float e=allowed?exp(scores[k*64+query]-mx):0;
#elif CACHE_SCORES == 2
        float e=allowed?exp(local_scores[k]-mx):0;
#else
        float dot=0;[unroll]for(uint d=0;d<16;d++)dot+=q[d]*keys[k*16+d];
        float e=allowed?exp(dot*qnorm*key_norm[k]*scale+W(6144+query*64+k)-mx):0;
#endif
        den+=e;
        [unroll]for(uint d=0;d<16;d++)a[d]+=e*values[k*16+d];
    }
    [loop]for(uint c=0;c<32;c++){float z=0;[unroll]for(uint d=0;d<16;d++)z+=(a[d]/den)*W(5632+c*16+d);outp[t*32+c]=F(z+feat_in[t*32+c]*W(10273+c));}
}
