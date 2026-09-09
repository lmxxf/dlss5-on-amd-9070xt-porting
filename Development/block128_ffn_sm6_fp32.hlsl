#ifndef WIDTH
#define WIDTH 480
#endif
#ifndef HEIGHT
#define HEIGHT 272
#endif
ByteAddressBuffer weights:register(t0);
StructuredBuffer<float> input:register(t1);
StructuredBuffer<float> feature_unused:register(t2);
StructuredBuffer<float> hidden_input:register(t3);
RWStructuredBuffer<float> features:register(u0);
RWStructuredBuffer<float> output_unused:register(u1);
RWStructuredBuffer<float> hidden_output:register(u2);
float weight(uint index){return asfloat(weights.Load(index*4));}
float quantize_e4m3(float value){if(value==0)return 0;float sign_value=value<0?-1:1,magnitude=abs(value);if(magnitude<0.015625)return sign_value*round(magnitude*512)/512;float exponent=clamp(floor(log2(magnitude)),-6.0,8.0),mantissa=round((magnitude/exp2(exponent)-1)*8);if(mantissa>=8){mantissa=0;exponent+=1;}return sign_value*min(exp2(exponent)*(1+mantissa/8),448.0);}
float fast_activation(float value){value=clamp(value,-4.0,4.0);return value*(0.89453125+value*(0.447265625-0.055908203125*abs(value)));}
[numthreads(64,1,1)]void ffn_expand(uint3 id:SV_DispatchThreadID){
    const uint index=id.x;if(index>=WIDTH*HEIGHT*160)return;const uint token=index/160,j=index%160;float value=0;
    [loop]for(uint channel=0;channel<128;++channel)value+=input[token*128+channel]*weight(j*128+channel);
    hidden_output[token*384+j]=fast_activation(value);
}
[numthreads(64,1,1)]void ffn_project(uint3 id:SV_DispatchThreadID){
    const uint index=id.x;if(index>=WIDTH*HEIGHT*128)return;const uint token=index/128,channel=index%128;float value=0;
    [loop]for(uint j=0;j<160;++j)value+=hidden_input[token*384+j]*weight(20480+channel*160+j);
    value+=input[index]*weight(90116+channel);features[index]=quantize_e4m3(value);
}
