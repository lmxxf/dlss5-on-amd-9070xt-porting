#ifndef CHANNELS
#define CHANNELS 64
#endif
StructuredBuffer<float> input:register(t0);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;uint work_width;uint work_height;uint pad_x;uint pad_y;}
[numthreads(64,1,1)]void pack(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=work_width*work_height)return;int x=int(p%work_width)-int(pad_x),y=int(p/work_width)-int(pad_y);
 // Root SRVs do not carry a buffer length. Do not express this as a
 // select: an eagerly evaluated negative-coordinate load can fault.
 [branch]if(x<0||y<0||x>=int(width)||y>=int(height)){
  [loop]for(uint c=0;c<CHANNELS;c++)output[p*CHANNELS+c]=0;
  return;
 }
 [loop]for(uint c=0;c<CHANNELS;c++)output[p*CHANNELS+c]=input[(uint(y)*width+uint(x))*CHANNELS+c];
}
[numthreads(64,1,1)]void crop(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=width*height)return;uint source=((p/width+pad_y)*work_width+p%width+pad_x)*CHANNELS;
 [loop]for(uint c=0;c<CHANNELS;c++)output[p*CHANNELS+c]=input[source+c];
}
