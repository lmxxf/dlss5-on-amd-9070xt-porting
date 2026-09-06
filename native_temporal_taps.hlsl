// Diagnostic only: replace the sampler in an isolated lab directory, never
// deploy as neural output. Four float4 records per pixel, first count/4 pixels.
#define main temporal_production_main
// Lab setup copies the unmodified production shader to this name first.
#include "native_temporal_production.hlsl"
#undef main
float2 tap_uv(float2 xy) {
    precise float2 normalized=xy*float2(inverse_width,inverse_height);
    precise float2 pixel=float2(
        float(fma(double(normalized.x),double(width),0.0)),
        float(fma(double(normalized.y),double(height),0.0)));
    precise float2 uv=pixel*float2(inverse_width,inverse_height);
    return uv;
}
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
    if(id.x>=count/4)return;
    float3 px,py,wx,wy;
    axis(coordinates[id.x].x,width,px,wx);
    axis(coordinates[id.x].y,height,py,wy);
    reconstructed[4*id.x]=float4(tap_uv(float2(px.y,py.x)),tap_uv(float2(px.x,py.y)));
    reconstructed[4*id.x+1]=float4(tap_uv(float2(px.y,py.y)),tap_uv(float2(px.y,py.z)));
    reconstructed[4*id.x+2]=float4(tap_uv(float2(px.z,py.y)),wx.x,wx.y);
    reconstructed[4*id.x+3]=float4(wx.z,wy);
}
