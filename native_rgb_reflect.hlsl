// Valid HWC RGBA buffer -> reflected processing image in8x8 tile-major RGBA.
StructuredBuffer<float4> input : register(t0);
RWStructuredBuffer<float4> output : register(u0);
cbuffer Geometry : register(b0) {
 uint valid_width; uint valid_height; uint processing_width; uint processing_height;
}
uint reflect_coordinate(uint p,uint extent){
 uint period=2*(extent-1),r=p%period;
 return r<extent?r:period-r;
}
[numthreads(8,8,1)]
void main(uint3 group:SV_GroupID,uint3 lane:SV_GroupThreadID){
 uint2 p=group.xy*8+lane.xy;
 if(p.x>=processing_width||p.y>=processing_height)return;
 uint2 source=uint2(reflect_coordinate(p.x,valid_width),reflect_coordinate(p.y,valid_height));
 uint tile=group.y*(processing_width/8)+group.x;
 output[tile*64+lane.y*8+lane.x]=input[source.y*valid_width+source.x];
}
