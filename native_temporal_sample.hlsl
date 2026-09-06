// Controlled temporal reconstruction. Coordinates are supplied pixel centers
// AFTER motion/transform; generating those coordinates is a separate contract.
StructuredBuffer<float4> history : register(t0);
StructuredBuffer<float2> coordinates : register(t1);
#if TEMPORAL_RECIPROCAL_TABLE
StructuredBuffer<uint> reciprocal_table : register(t2);
#endif
RWStructuredBuffer<float4> reconstructed : register(u0);
cbuffer Geometry : register(b0) { uint width; uint height; uint count; float inverse_width; float inverse_height; }
float temporal_reciprocal(float x) {
#if TEMPORAL_RECIPROCAL_TABLE
    if(x>=.5&&x<2.0) {
        uint bits=asuint(x);
        int shift=127-int((bits>>23)&255);
        return asfloat(uint(int(reciprocal_table[bits&0x7fffff])+shift*8388608));
    }
#endif
    return 1.0/x;
}

void axis(float p,uint extent,out float3 positions,out float3 weights) {
#if NORMALIZED_COORDINATES
    precise float center=floor(mad(p,float(extent),-.5))+.5;
    // Preserve the product bits through cancellation; measured mad rounded
    // before subtracting center on AMD. Keep this scoped to the fraction.
    precise float t=saturate(float(fma(double(p),double(extent),-double(center))));
#else
    precise float center=floor(p-.5)+.5;
    precise float t=saturate(p-center);
#endif
    precise float t2=t*t,t3=t2*t;
    precise float sum=t+t3;
    precise float left=mad(sum,-.5,t2);
    precise float scaled=t2*2.5;
    precise float inner=mad(t3,1.5,-scaled);
    inner=inner+1;
    precise float right=(t3-t2)*.5;
    precise float other=1-left;other=other-inner;other=other-right;
    precise float middle=inner+other;
    precise float reciprocal=temporal_reciprocal(middle);
    precise float position=float(fma(double(other),double(reciprocal),double(center)));
    positions=clamp(float3(center-1,position,center+2),.5,float(extent)-.5);
    weights=float3(left,middle,right);
}
float3 fetch(float2 xy) {
    precise float2 normalized=xy*float2(inverse_width,inverse_height);
    // Original applies the history subrect transform even for the full-size
    // zero-offset case. Keep both roundings instead of cancelling dimensions.
    precise float2 history_pixel=float2(
        float(fma(double(normalized.x),double(width),0.0)),
        float(fma(double(normalized.y),double(height),0.0)));
    precise float2 uv=history_pixel*float2(inverse_width,inverse_height);
    precise float2 fixed_uv=floor(uv*2097152.0);
    // Quantize directly from 21-bit UV to 8-bit texel fractions. Converting
    // the product to float first can round across a half-grid boundary.
    // Split at 13 bits so every product fits uint32 even at extent 16384.
    uint2 fixed_bits=uint2(clamp(fixed_uv,0,2097152.0));
    uint2 scaled=(fixed_bits>>13)*uint2(width,height)
        +(((fixed_bits&8191)*uint2(width,height)+4096)>>13);
    precise float2 pixel=float2(max(scaled,128)-128)/256.0;
    precise float2 p=clamp(pixel,0,float2(width-1,height-1));
    uint2 lo=uint2(floor(p)),hi=min(lo+1,uint2(width-1,height-1));
    precise float2 f=floor((p-float2(lo))*256.0+.5)/256.0;
    precise float corner=floor(((1-f.x)*(1-f.y))*256.0+.5)/256.0;
    precise float adjacent_x=(1-f.y)-corner;
    precise float adjacent_y=(1-f.x)-corner;
    precise float opposite=f.x-adjacent_x;
    precise float4 w=float4(corner,adjacent_x,adjacent_y,opposite);
    // Products on binary /256 weights are accumulated in a wider intermediate
    // to match the reference's single float rounding at the texture boundary.
    double3 v=double3(history[lo.y*width+lo.x].xyz)*double(w.x);
    v+=double3(history[lo.y*width+hi.x].xyz)*double(w.y);
    v+=double3(history[hi.y*width+lo.x].xyz)*double(w.z);
    v+=double3(history[hi.y*width+hi.x].xyz)*double(w.w);
    return float3(v);
}
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
    if(id.x>=count)return;
    float3 px,py,wx,wy;axis(coordinates[id.x].x,width,px,wx);axis(coordinates[id.x].y,height,py,wy);
    precise float top=wx.y*wy.x,left=wx.x*wy.y,center=wx.y*wy.y,bottom=wx.y*wy.z,right=wx.z*wy.y;
    precise float3 result=fetch(float2(px.y,py.x))*top;
    result=float3(fma(double3(fetch(float2(px.x,py.y))),double3(left,left,left),double3(result)));
    result=float3(fma(double3(fetch(float2(px.y,py.y))),double3(center,center,center),double3(result)));
    result=float3(fma(double3(fetch(float2(px.y,py.z))),double3(bottom,bottom,bottom),double3(result)));
    result=float3(fma(double3(fetch(float2(px.z,py.y))),double3(right,right,right),double3(result)));
    precise float total=left+top;total=total+center;total=total+bottom;total=total+right;
    precise float reciprocal=temporal_reciprocal(total);
    reconstructed[id.x]=float4(result*reciprocal,1);
}
