// Isolated diagnostic replacement, NOT neural RGB output.
#define main temporal_production_main
#include "native_temporal_production.hlsl"
#undef main
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
    if(id.x>=count/4)return;
    precise float2 p=coordinates[id.x];
    precise float2 extent=float2(width,height);
    precise float2 inverse=float2(inverse_width,inverse_height);
    precise float2 before_floor=mad(p,extent,-.5);
    precise float2 center=floor(before_floor)+.5;
    precise float2 t=saturate(float2(
        float(fma(double(p.x),double(extent.x),-double(center.x))),
        float(fma(double(p.y),double(extent.y),-double(center.y)))));
    precise float2 left=max(center-1,.5);
    precise float2 first=left*inverse;
    precise float2 pixel=mad(first,extent,0);
    precise float2 last=pixel*inverse;
    reconstructed[4*id.x]=float4(p,before_floor);
    reconstructed[4*id.x+1]=float4(center,t);
    reconstructed[4*id.x+2]=float4(left,first);
    reconstructed[4*id.x+3]=float4(pixel,last);
}
