#ifndef WIDTH
#define WIDTH 1920
#endif
#ifndef HEIGHT
#define HEIGHT 1088
#endif
ByteAddressBuffer w:register(t0);
StructuredBuffer<float> inp:register(t1),qkv_in:register(t3);
#ifdef PACKED_FEATURE
ByteAddressBuffer feat_in:register(t2);
#ifdef NATIVE_HALF
typedef vector<float16_t,2> HalfPair;
#endif
#else
StructuredBuffer<float> feat_in:register(t2);
#endif
RWStructuredBuffer<float> feat:register(u0),outp:register(u1),qkv_out:register(u2);
float W(uint i){return asfloat(w.Load(i*4));}
[numthreads(64,1,1)]
void main(uint3 group:SV_GroupID,uint3 lane:SV_GroupThreadID){
    uint z=(group.y*65535+group.x)*64+lane.x;
    uint t=z/16,o=z%16;
    if(t>=WIDTH*HEIGHT)return;
    precise float a=0,b=0,c=0;
    [unroll]for(uint j=0;j<16;j++){
#ifdef PACKED_FEATURE
#ifdef NATIVE_HALF
        HalfPair packed=feat_in.Load<HalfPair>((t*16+j)*4);
        float e=packed.x,v=packed.y;
#else
        uint packed=feat_in.Load((t*16+j)*4);
        float e=f16tof32(packed&65535),v=f16tof32(packed>>16);
#endif
#else
        float e=feat_in[t*32+j*2],v=feat_in[t*32+j*2+1];
#endif
        a+=e*W(4096+o*16+j)+v*W(4352+o*16+j);
        b+=e*W(4608+o*16+j)+v*W(4864+o*16+j);
        c+=e*W(5120+o*16+j)+v*W(5376+o*16+j);
    }
    qkv_out[t*48+o]=a;qkv_out[t*48+16+o]=b;qkv_out[t*48+32+o]=c;
}
