#ifndef MATRIX_CHANNELS
#define MATRIX_CHANNELS 256
#endif
StructuredBuffer<float> input:register(t0);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;uint raster_width;uint raster_height;uint pad_x;uint pad_y;}
// Caller supplies finite FP8-lattice features; conversion to half is exact.
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){
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
}
