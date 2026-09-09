// Matches Window1080Pass descriptor layout; one group per head/window.
ByteAddressBuffer qkvRaw:register(t0);
StructuredBuffer<float> qkv:register(t1),bias:register(t2),scale:register(t3),attentionIn:register(t4);
RWStructuredBuffer<float> qkvOut:register(u0),attentionOut:register(u1);
RWByteAddressBuffer attentionHalf:register(u2);
groupshared float keys[1024],values[1024],knorm[64];
[numthreads(64,1,1)]
void main(uint3 gid:SV_GroupID,uint3 lane:SV_GroupThreadID){
    uint group=gid.x+gid.y*65535,head=group%HEADS,window=group/HEADS,query=lane.x;
    if(window>=(WIDTH/8)*(HEIGHT/8))return;
    uint rx=(window%(WIDTH/8))*8+query%8,ry=(window/(WIDTH/8))*8+query/8;
    uint x=SHIFT_X?(rx+4)%WIDTH:rx,y=SHIFT_Y?(ry+4)%HEIGHT:ry,t=y*WIDTH+x;
    const uint A=HEADS*16,Q=A*3;uint base=head*16;
    float q[16],qq=0,kk=0;
    [unroll]for(uint d=0;d<16;d++){q[d]=qkv[t*Q+base+d];qq+=q[d]*q[d];float k=qkv[t*Q+A+base+d];kk+=k*k;keys[query*16+d]=k;values[query*16+d]=qkv[t*Q+2*A+base+d];}
    knorm[query]=rsqrt(max(kk,1e-12));GroupMemoryBarrierWithGroupSync();
    float qnorm=rsqrt(max(qq,1e-12)),s=scale[head],mx=-3.4e38;
    [loop]for(uint k=0;k<64;k++){float dot=0;[unroll]for(uint d=0;d<16;d++)dot+=q[d]*keys[k*16+d];mx=max(mx,dot*qnorm*knorm[k]*s+bias[head*4096+query*64+k]);}
    float den=0,a[16];[unroll]for(uint d=0;d<16;d++)a[d]=0;
    [loop]for(uint k=0;k<64;k++){float dot=0;[unroll]for(uint d=0;d<16;d++)dot+=q[d]*keys[k*16+d];float e=exp(dot*qnorm*knorm[k]*s+bias[head*4096+query*64+k]-mx);den+=e;[unroll]for(uint d=0;d<16;d++)a[d]+=e*values[k*16+d];}
    [unroll]for(uint d=0;d<16;d++)attentionOut[t*A+base+d]=a[d]/den;
}
