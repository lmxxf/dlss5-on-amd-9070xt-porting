StructuredBuffer<float4> input:register(t0);
RWStructuredBuffer<float4> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;uint work_width;uint work_height;uint pad_x;uint pad_y;}
// FAST PATH: 128 threads per pixel, one float4 each (was one thread looping over 512 floats).
[numthreads(64,1,1)]void pack(uint3 id:SV_DispatchThreadID){
 uint p=id.x/128,q=id.x%128;[branch]if(p>=work_width*work_height)return;
 int x=int(p%work_width)-int(pad_x),y=int(p/work_width)-int(pad_y);
 if(width==4)x=(x%4+4)%4;if(height==4)y=(y%4+4)%4;
 [branch]if(x<0||y<0||x>=int(width)||y>=int(height)){output[p*128+q]=0;return;}
 output[p*128+q]=input[(uint(y)*width+uint(x))*128+q];
}
[numthreads(64,1,1)]void crop(uint3 id:SV_DispatchThreadID){
 uint p=id.x/128,q=id.x%128;[branch]if(p>=width*height)return;
 uint source=((p/width+pad_y)*work_width+p%width+pad_x)*128;
 output[p*128+q]=input[source+q];
}
