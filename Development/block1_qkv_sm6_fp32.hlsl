#ifndef WIDTH
#define WIDTH 3840
#endif
#ifndef HEIGHT
#define HEIGHT 2160
#endif
ByteAddressBuffer weights:register(t0);StructuredBuffer<float> input_unused:register(t1),features:register(t2),qkv_unused:register(t3);RWStructuredBuffer<float> feature_unused:register(u0),output_unused:register(u1),qkv:register(u2);
float weight(uint i){return asfloat(weights.Load(i*4));}
[numthreads(64,1,1)]void qkv_precompute(uint3 group:SV_GroupID,uint3 thread:SV_GroupThreadID){uint t=(group.y*65535+group.x)*64+thread.x;if(t>=WIDTH*HEIGHT)return;[unroll]for(uint o=0;o<16;o++){float q=0,k=0,v=0;[unroll]for(uint j=0;j<16;j++){float e=features[t*32+j*2],z=features[t*32+j*2+1];q+=e*weight(4096+o*16+j)+z*weight(4352+o*16+j);k+=e*weight(4608+o*16+j)+z*weight(4864+o*16+j);v+=e*weight(5120+o*16+j)+z*weight(5376+o*16+j);}qkv[t*48+o]=q;qkv[t*48+16+o]=k;qkv[t*48+32+o]=v;}}
