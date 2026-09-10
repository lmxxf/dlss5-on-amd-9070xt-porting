// FAST PATH: one-time operand copies of a ViT activation (values are on the FP8 grid, so both are exact).
// pack8: f32 [tokens][C] -> E4M3 bytes; pack16: f32 -> f16. Consumers load A tiles straight from memory
// instead of restaging the tile in LDS at every K step.
StructuredBuffer<float> input:register(t0);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint values;}
uint E4M3(float v){uint b=asuint(v),a=b&0x7fffffffu,sg=(b>>24)&0x80u;if(a==0)return sg;float m=abs(v);if(m<0.015625)return sg|uint(round(m*512.0));uint e=(a>>23)-127+7,mant=(a>>20)&7u;if(e>15)return sg|0x7eu;return sg|(e<<3)|mant;}
#ifndef NATIVE_VIT_TILED
#define NATIVE_VIT_TILED 0
#endif
#if NATIVE_VIT_TILED
/* FAST PATH (DLSS5_VIT_TILED): [token][1024] stored as tiles (token/16, k/32) of [16][32] elements at ((token/16)*32+k/32)*512*E (no 1024-element row strides). */
uint tiled(uint i,uint e){uint tok=i/1024,k=i%1024;return (((tok/16)*32+k/32)*512+(tok%16)*32+(k%32))*e;}
#else
uint tiled(uint i,uint e){return i*e;}
#endif
[numthreads(64,1,1)]void pack8(uint3 id:SV_DispatchThreadID){uint i=id.x*4;if(i>=values)return;output.Store(tiled(i,1),E4M3(input[i])|(E4M3(input[i+1])<<8)|(E4M3(input[i+2])<<16)|(E4M3(input[i+3])<<24));}
[numthreads(64,1,1)]void pack16(uint3 id:SV_DispatchThreadID){uint i=id.x*2;if(i>=values)return;output.Store(tiled(i,2),f32tof16(input[i])|(f32tof16(input[i+1])<<16));}
