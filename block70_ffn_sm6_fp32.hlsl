#define WIDTH 3840
#define HEIGHT 2160
ByteAddressBuffer weights:register(t0);StructuredBuffer<float> input:register(t1),feature_unused:register(t2),qkv_unused:register(t3);RWStructuredBuffer<float> features:register(u0),output_unused:register(u1),qkv_output_unused:register(u2);
float weight(uint i){return asfloat(weights.Load(i*4));}
uint input_index(uint t,uint c){uint x=t%WIDTH,y=t/WIDTH,tile=(y/8)*(WIDTH/8)+x/8,local=((y%8)*8+x%8)*32+c;return tile*2048+local;}
float fp8(float x){if(x==0)return 0;float s=x<0?-1:1,a=abs(x);if(a<.015625)return s*round(a*512)/512;float e=clamp(floor(log2(a)),-6.,8.),m=round((a/exp2(e)-1)*8);if(m>=8){m=0;e+=1;}return s*min(exp2(e)*(1+m/8),448.);}
[numthreads(64,1,1)]void ffn(uint3 group:SV_GroupID,uint3 thread:SV_GroupThreadID){uint t=(group.y*65535+group.x)*64+thread.x;if(t>=WIDTH*HEIGHT)return;float h[64];[unroll]for(uint j=0;j<64;j++){float a=0;[unroll]for(uint c=0;c<32;c++)a+=input[input_index(t,c)]*weight(j*32+c);a=clamp(a,-4.,4.);h[j]=a*(.89453125+a*(.447265625-.055908203125*abs(a)));}[unroll]for(uint c=0;c<32;c++){float v=0;[unroll]for(uint j=0;j<64;j++)v+=h[j]*weight(2048+c*64+j);v+=input[input_index(t,c)]*weight(10241+c);features[t*32+c]=fp8(v);}}
