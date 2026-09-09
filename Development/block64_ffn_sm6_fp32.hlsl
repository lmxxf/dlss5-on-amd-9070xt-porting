#ifndef WIDTH
#define WIDTH 960
#endif
#ifndef HEIGHT
#define HEIGHT 544
#endif
ByteAddressBuffer weights:register(t0);
StructuredBuffer<float> input:register(t1);
StructuredBuffer<float> feature_unused:register(t2);
StructuredBuffer<float> qkv_unused:register(t3);
RWStructuredBuffer<float> features:register(u0);
RWStructuredBuffer<float> output_unused:register(u1);
RWStructuredBuffer<float> qkv_output_unused:register(u2);
float weight(uint index){return asfloat(weights.Load(index*4));}
float quantize_e4m3(float value){if(value==0)return 0;float sign_value=value<0?-1:1,magnitude=abs(value);if(magnitude<0.015625)return sign_value*round(magnitude*512)/512;float exponent=clamp(floor(log2(magnitude)),-6.0,8.0),mantissa=round((magnitude/exp2(exponent)-1)*8);if(mantissa>=8){mantissa=0;exponent+=1;}return sign_value*min(exp2(exponent)*(1+mantissa/8),448.0);}
float fast_activation(float value){value=clamp(value,-4.0,4.0);return value*(0.89453125+value*(0.447265625-0.055908203125*abs(value)));}
[numthreads(64,1,1)]
void ffn(uint3 id:SV_DispatchThreadID){
    const uint token=id.x;if(token>=WIDTH*HEIGHT)return;
    float hidden[96];
    [unroll]for(uint j=0;j<96;++j){float value=0;[unroll]for(uint channel=0;channel<64;++channel)value+=input[token*64+channel]*weight(j*64+channel);hidden[j]=fast_activation(value);}
    [unroll]for(uint channel=0;channel<64;++channel){float value=0;[unroll]for(uint j=0;j<96;++j)value+=hidden[j]*weight(6144+channel*96+j);value+=input[token*64+channel]*weight(12288+channel);features[token*64+channel]=quantize_e4m3(value);}
}
