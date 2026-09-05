#ifndef WIDTH
#define WIDTH 1920
#endif
#ifndef HEIGHT
#define HEIGHT 1088
#endif
ByteAddressBuffer weights : register(t0);
StructuredBuffer<float> input : register(t1);
StructuredBuffer<float> feature_unused : register(t2);
StructuredBuffer<float> qkv_unused : register(t3);
RWStructuredBuffer<float> features : register(u0);
RWStructuredBuffer<float> output_unused : register(u1);
RWStructuredBuffer<float> qkv_output_unused : register(u2);
float weight(uint index) { return asfloat(weights.Load(index * 4)); }
float quantize_e4m3(float value) {
#ifdef BIT_QUANT
    // Finite FP32 -> E4M3, round-to-nearest-even, saturating at 448.
    float magnitude=abs(value);
    if(magnitude<0.015625) return round(value*512.0)/512.0;
    uint bits=asuint(min(magnitude,448.0));
    bits=(bits+0x0007ffff+((bits>>20)&1))&0xfff00000;
    return value<0?-asfloat(bits):asfloat(bits);
#else
    if (value == 0) return 0;
    const float sign_value=value<0?-1:1,magnitude=abs(value);
    if (magnitude<0.015625) return sign_value*round(magnitude*512)/512;
    float exponent=clamp(floor(log2(magnitude)),-6.0,8.0);
    float mantissa=round((magnitude/exp2(exponent)-1)*8);
    if(mantissa>=8){mantissa=0;exponent+=1;}
    return sign_value*min(exp2(exponent)*(1+mantissa/8),448.0);
#endif
}
[numthreads(64,1,1)]
void ffn(uint3 group:SV_GroupID,uint3 thread:SV_GroupThreadID){
    const uint token=(group.y*65535+group.x)*64+thread.x;
    if(token>=WIDTH*HEIGHT)return;
    float hidden[64];
    [unroll]for(uint j=0;j<64;++j){
        float value=0;
        [unroll]for(uint channel=0;channel<32;++channel)value+=input[token*32+channel]*weight(j*32+channel);
        value=clamp(value,-4.0,4.0);
        hidden[j]=value*(0.89453125+value*(0.447265625-0.055908203125*abs(value)));
    }
    [unroll]for(uint channel=0;channel<32;++channel){
        float value=0;
        [unroll]for(uint j=0;j<64;++j)value+=hidden[j]*weight(2048+channel*64+j);
        value+=input[token*32+channel]*weight(10241+channel);
        features[token*32+channel]=quantize_e4m3(value);
    }
}
