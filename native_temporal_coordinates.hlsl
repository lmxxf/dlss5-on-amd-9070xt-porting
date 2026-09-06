// No optional scalar/depth-neighborhood slot18 path. Host supplies the captured
// motion subrect transform and UV displacement scale; no inferred sign/scaling.
StructuredBuffer<float4> motion : register(t0);
RWStructuredBuffer<float2> coordinates : register(u0);
cbuffer Geometry : register(b0) {
    uint valid_width; uint valid_height; uint processing_width; uint processing_height;
    uint motion_width; uint motion_height; float motion_offset_x; float motion_offset_y;
    float motion_extent_x; float motion_extent_y; float motion_uv_scale_x; float motion_uv_scale_y;
    float valid_inverse_width; float valid_inverse_height; float motion_inverse_width; float motion_inverse_height;
}
float2 fetch_motion(float2 uv) {
    precise float2 fixed_uv=floor(uv*2097152.0);
    precise float2 pixel=fixed_uv*(float2(motion_width,motion_height)/2097152.0)-.5;
    precise float2 p=clamp(pixel,0,float2(motion_width-1,motion_height-1));
    uint2 lo=uint2(floor(p)),hi=min(lo+1,uint2(motion_width-1,motion_height-1));
    precise float2 f=floor((p-float2(lo))*256.0+.5)/256.0;
    precise float corner=floor(((1-f.x)*(1-f.y))*256.0+.5)/256.0;
    precise float adjacent_x=(1-f.y)-corner;
    precise float adjacent_y=(1-f.x)-corner;
    precise float opposite=f.x-adjacent_x;
    precise float4 w=float4(corner,adjacent_x,adjacent_y,opposite);
    double2 v=double2(motion[lo.y*motion_width+lo.x].xy)*double(w.x);
    v+=double2(motion[lo.y*motion_width+hi.x].xy)*double(w.y);
    v+=double2(motion[hi.y*motion_width+lo.x].xy)*double(w.z);
    v+=double2(motion[hi.y*motion_width+hi.x].xy)*double(w.w);
    return float2(v);
}
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
    if(id.x>=processing_width*processing_height)return;
    uint2 p=uint2(id.x%processing_width,id.x/processing_width);
    p.x=p.x<valid_width?p.x:2*valid_width-p.x-2;
    p.y=p.y<valid_height?p.y:2*valid_height-p.y-2;
    precise float2 reciprocal=float2(valid_inverse_width,valid_inverse_height);
    precise float2 uv=(float2(p)+.5)*reciprocal;
    precise float2 sample_uv=mad(uv,float2(motion_extent_x,motion_extent_y),float2(motion_offset_x,motion_offset_y));
    precise float2 motion_reciprocal=float2(motion_inverse_width,motion_inverse_height);
    sample_uv=sample_uv*motion_reciprocal;
    float2 vectors=fetch_motion(sample_uv);
    precise float2 previous_uv=float2(
        float(fma(double(vectors.x),double(motion_uv_scale_x),double(uv.x))),
        float(fma(double(vectors.y),double(motion_uv_scale_y),double(uv.y))));
#if NORMALIZED_COORDINATES
    coordinates[id.x]=previous_uv;
#else
    coordinates[id.x]=previous_uv*float2(valid_width,valid_height);
#endif
}
