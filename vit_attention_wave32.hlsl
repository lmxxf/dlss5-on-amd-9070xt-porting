#ifndef TOKENS
#define TOKENS 2160
#endif
ByteAddressBuffer weights_unused:register(t0);
StructuredBuffer<float> source_unused:register(t1),scales_unused:register(t2);
RWStructuredBuffer<float> branch_unused:register(u0),hidden_unused:register(u1),qkv:register(u2),attention:register(u3),result_unused:register(u4);
float quantize_e4m3(float value){if(value==0)return 0;float sign_value=value<0?-1:1,magnitude=abs(value);if(magnitude<.015625)return sign_value*round(magnitude*512)/512;float exponent=clamp(floor(log2(magnitude)),-6.,8.),mantissa=round((magnitude/exp2(exponent)-1)*8);if(mantissa>=8){mantissa=0;exponent+=1;}return sign_value*min(exp2(exponent)*(1+mantissa/8),448.);}
[WaveSize(32)]
[numthreads(32,1,1)]
void attention_cs(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){
    const uint token=group.x,head=group.y,channel=head*32+lane;
    const float query=qkv[(token*3)*1024+channel];
    float maximum=-3.4e38;
    [loop]for(uint key=0;key<TOKENS;++key){
        const float dot=WaveActiveSum(query*qkv[(key*3+1)*1024+channel]);
        maximum=max(maximum,dot);
    }
    float denominator=0,value=0;
    [loop]for(uint key=0;key<TOKENS;++key){
        const float dot=WaveActiveSum(query*qkv[(key*3+1)*1024+channel]);
        const float coefficient=exp(dot-maximum);
        denominator+=coefficient;
        value+=coefficient*qkv[(key*3+2)*1024+channel];
    }
    attention[token*1024+channel]=quantize_e4m3(value/denominator);
}
