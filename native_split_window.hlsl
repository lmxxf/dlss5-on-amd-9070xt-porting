StructuredBuffer<float> input:register(t0);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;uint work_width;uint work_height;uint pad_x;uint pad_y;}
[numthreads(64,1,1)]void pack(uint3 id:SV_DispatchThreadID){
 uint p=id.x;[branch]if(p>=work_width*work_height)return;
 int x=int(p%work_width)-int(pad_x),y=int(p/work_width)-int(pad_y);
 if(width==4)x=(x%4+4)%4;if(height==4)y=(y%4+4)%4;
 [branch]if(x<0||y<0||x>=int(width)||y>=int(height)){
  [loop]for(uint c=0;c<512;c++)output[p*512+c]=0;return;
 }
 [loop]for(uint c=0;c<512;c++)output[p*512+c]=input[(uint(y)*width+uint(x))*512+c];
}
[numthreads(64,1,1)]void crop(uint3 id:SV_DispatchThreadID){
 uint p=id.x;[branch]if(p>=width*height)return;
 uint source=((p/width+pad_y)*work_width+p%width+pad_x)*512;
 [loop]for(uint c=0;c<512;c++)output[p*512+c]=input[source+c];
}
