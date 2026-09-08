// FAST PATH: one-time operand copies of a ViT activation (values are on the FP8 grid, so both are exact).
// pack8: f32 [tokens][C] -> E4M3 bytes; pack16: f32 -> f16. Consumers load A tiles straight from memory
// instead of restaging the tile in LDS at every K step.
StructuredBuffer<float> input:register(t0);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint values;}
uint E4M3(float v){uint b=asuint(v),a=b&0x7fffffffu,sg=(b>>24)&0x80u;if(a==0)return sg;float m=abs(v);if(m<0.015625)return sg|uint(round(m*512.0));uint e=(a>>23)-127+7,mant=(a>>20)&7u;if(e>15)return sg|0x7eu;return sg|(e<<3)|mant;}
[numthreads(64,1,1)]void pack8(uint3 id:SV_DispatchThreadID){uint i=id.x*4;if(i>=values)return;output.Store(i,E4M3(input[i])|(E4M3(input[i+1])<<8)|(E4M3(input[i+2])<<16)|(E4M3(input[i+3])<<24));}
[numthreads(64,1,1)]void pack16(uint3 id:SV_DispatchThreadID){uint i=id.x*2;if(i>=values)return;output.Store(i*2,f32tof16(input[i])|(f32tof16(input[i+1])<<16));}
