// Game-side temporal feed.
// motion:  FSR motion-vector texture (RG16F, render-resolution grid, UV units) ->
//          float4 buffer in 1080p pixel units, the contract the captured NGX
//          preblock parameters use (displacement scale 1/1920, 1/1080).
// history: previous network output RGB (1920x1152x3, working-surface encoding)
//          -> float4 [1080][1920], the same space as the temporal test fixture.
cbuffer Geometry:register(b0){uint motion_width;uint motion_height;float scale_x;float scale_y;float max_px;} /* max_px: motion longer than this (output pixels) becomes zero; 0 = off */
#if FEED_MOTION
Texture2D<float2> motion_texture:register(t0);
RWStructuredBuffer<float4> motion:register(u0);
[numthreads(16,16,1)]void motion_main(uint3 id:SV_DispatchThreadID){
 if(id.x>=motion_width||id.y>=motion_height)return;
 float2 v=motion_texture.Load(int3(id.xy,0));
 float2 px=float2(v.x*scale_x,v.y*scale_y);
 if(max_px>0&&(abs(px.x)>max_px||abs(px.y)>max_px||any(isnan(px))))px=0;
 motion[id.y*motion_width+id.x]=float4(px,0,0);
}
#else
StructuredBuffer<float> rgb:register(t0);
RWStructuredBuffer<float4> history:register(u0);
[numthreads(16,16,1)]void history_main(uint3 id:SV_DispatchThreadID){
 if(id.x>=1920||id.y>=1080)return;
 uint p=(id.y*1920+id.x)*3;
 history[id.y*1920+id.x]=float4(rgb[p],rgb[p+1],rgb[p+2],1);
}
#endif
