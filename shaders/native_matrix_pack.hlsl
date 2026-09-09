#ifndef MATRIX_CHANNELS
#define MATRIX_CHANNELS 256
#endif
#ifndef NATIVE_FP8_SOURCE
#define NATIVE_FP8_SOURCE 0
#endif
#if NATIVE_FP8_SOURCE
// FAST PATH: the source raster is already E4M3 bytes; the mapped pack is a byte gather.
ByteAddressBuffer input8:register(t0);
#else
StructuredBuffer<float> input:register(t0);
#endif
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;uint raster_width;uint raster_height;uint pad_x;uint pad_y;}
// E4M3 bits of an FP8-lattice value (exact by construction; ties cannot occur).
uint E4M3(float v){uint b=asuint(v),a=b&0x7fffffffu,sg=(b>>24)&0x80u;if(a==0)return sg;float m=abs(v);if(m<0.015625)return sg|uint(round(m*512.0));uint e=(a>>23)-127+7,mant=(a>>20)&7u;if(e>15)return sg|0x7eu;return sg|(e<<3)|mant;}
// Caller supplies finite FP8-lattice features; conversion to half is exact.
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){
#if NATIVE_FP8_OPERANDS
 // Four E4M3 bytes per store.
 uint q=id.x+id.y*4194240u;if(q>=width*height*(MATRIX_CHANNELS/4))return;
#if MAPPED_INPUT
 uint p=q/(MATRIX_CHANNELS/4),quad=q%(MATRIX_CHANNELS/4);int x=int(p%width)-int(pad_x),y=int(p/width)-int(pad_y);
 [branch]if(x<0||y<0||x>=int(raster_width)||y>=int(raster_height)){output.Store(q*4,0u);return;}
 uint src=(uint(y)*raster_width+uint(x))*(MATRIX_CHANNELS/4)+quad;
#else
 uint src=q;
#endif
#if NATIVE_FP8_SOURCE
 output.Store(q*4,input8.Load(src*4));
#else
 output.Store(q*4,E4M3(input[src*4])|(E4M3(input[src*4+1])<<8)|(E4M3(input[src*4+2])<<16)|(E4M3(input[src*4+3])<<24));
#endif
#else
 uint n=id.x+id.y*4194240u;if(n>=width*height*(MATRIX_CHANNELS/2))return;
#if MAPPED_INPUT
 // Read the unpadded raster directly; padded border tokens are zero, exactly as the shift pack wrote them.
 uint p=n/(MATRIX_CHANNELS/2),pair=n%(MATRIX_CHANNELS/2);int x=int(p%width)-int(pad_x),y=int(p/width)-int(pad_y);
 [branch]if(x<0||y<0||x>=int(raster_width)||y>=int(raster_height)){output.Store(n*4,0u);return;}
 uint src=(uint(y)*raster_width+uint(x))*(MATRIX_CHANNELS/2)+pair;
 output.Store(n*4,(f32tof16(input[src*2])&65535u)|(f32tof16(input[src*2+1])<<16));
#else
 output.Store(n*4,(f32tof16(input[n*2])&65535u)|(f32tof16(input[n*2+1])<<16));
#endif
#endif
}
