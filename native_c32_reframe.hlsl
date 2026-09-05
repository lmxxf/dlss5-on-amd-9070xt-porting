StructuredBuffer<float> source:register(t0);
RWStructuredBuffer<float> destination:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;uint work_width;uint work_height;uint shift_x;uint shift_y;}
[numthreads(64,1,1)]void pack(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=work_width*work_height)return;
 uint tile=p/64,x=(tile%(work_width/8))*8+p%8,y=(tile/(work_width/8))*8+(p%64)/8;
 int sx=int(x)-int(shift_x),sy=int(y)-int(shift_y);
 [unroll]for(uint c=0;c<32;c++)destination[p*32+c]=(sx>=0&&sy>=0&&sx<int(width)&&sy<int(height))?source[(sy*int(width)+sx)*32+c]:0;
}
[numthreads(64,1,1)]void crop(uint3 id:SV_DispatchThreadID){
 uint p=id.x;if(p>=width*height)return;uint x=p%width+shift_x,y=p/width+shift_y;
 [unroll]for(uint c=0;c<32;c++)destination[p*32+c]=source[(y*work_width+x)*32+c];
}
