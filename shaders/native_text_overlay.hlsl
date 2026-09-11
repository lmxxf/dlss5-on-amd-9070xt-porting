// DLSS5-AMD on-screen notice (native_text_overlay.h): a 5x7 bitmap font drawn by a compute pass into a raw buffer laid out like a
// strip of the host texture (row pitch `pitch` bytes, pixel format `mode`: 0 RGBA8, 1 BGRA8, 2 RGBA16F, 3 RGBA16 UNORM), which the
// frame then copies into the upscaler's output with CopyTextureRegion (a UAV on the host's own texture crashes D3D12Core in Magpie).
// Text arrives as 4 chars per uint (ASCII 32..95, upper case), drawn `scale` times enlarged on a black box.
RWByteAddressBuffer OutputBits : register(u0);
cbuffer Notice : register(b0) { uint4 text[4]; uint scale, count, mode, pitch; uint pad0, pad1, pad2, pad3; };
void Store(uint2 p,float4 v){
 if(mode==2){OutputBits.Store2(p.y*pitch+p.x*8,uint2(f32tof16(v.x)|(f32tof16(v.y)<<16),f32tof16(v.z)|(f32tof16(v.w)<<16)));return;}
 if(mode==3){uint4 q=uint4(round(saturate(v)*65535.0));OutputBits.Store2(p.y*pitch+p.x*8,uint2(q.x|(q.y<<16),q.z|(q.w<<16)));return;}
 uint4 q=uint4(round(saturate(v)*255.0));
 if(mode==1)OutputBits.Store(p.y*pitch+p.x*4,q.z|(q.y<<8)|(q.x<<16)|(q.w<<24));
 else OutputBits.Store(p.y*pitch+p.x*4,q.x|(q.y<<8)|(q.z<<16)|(q.w<<24));
}
static const uint2 FONT[64] = {
 uint2(0x00000000,0x00000000),
 uint2(0x00421084,0x00000001),
 uint2(0x00000000,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x08210888,0x00000002),
 uint2(0x88842082,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x88c00000,0x00000000),
 uint2(0x000f8000,0x00000000),
 uint2(0x18000000,0x00000003),
 uint2(0x02222200,0x00000000),
 uint2(0xa33ae62e,0x00000003),
 uint2(0x884210c4,0x00000003),
 uint2(0xc444422e,0x00000007),
 uint2(0xa304111f,0x00000003),
 uint2(0x11f4a988,0x00000002),
 uint2(0xa3083c3f,0x00000003),
 uint2(0xa317845c,0x00000003),
 uint2(0x8422221f,0x00000000),
 uint2(0xa317462e,0x00000003),
 uint2(0xd10f462e,0x00000001),
 uint2(0x08401080,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x01f07c00,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x631fc62e,0x00000004),
 uint2(0xe317c62f,0x00000003),
 uint2(0x8210843e,0x00000007),
 uint2(0xe318c62f,0x00000003),
 uint2(0xc217843f,0x00000007),
 uint2(0x4217843f,0x00000000),
 uint2(0xa31e843e,0x00000007),
 uint2(0x631fc631,0x00000004),
 uint2(0x8842108e,0x00000003),
 uint2(0xa3184210,0x00000003),
 uint2(0x52519531,0x00000004),
 uint2(0xc2108421,0x00000007),
 uint2(0x6318d771,0x00000004),
 uint2(0x631cd671,0x00000004),
 uint2(0xa318c62e,0x00000003),
 uint2(0x4217c62f,0x00000000),
 uint2(0x9358c62e,0x00000005),
 uint2(0x5257c62f,0x00000004),
 uint2(0xe107043e,0x00000003),
 uint2(0x0842109f,0x00000001),
 uint2(0xa318c631,0x00000003),
 uint2(0x14a8c631,0x00000001),
 uint2(0x775ac631,0x00000004),
 uint2(0x62a22a31,0x00000004),
 uint2(0x08422a31,0x00000001),
 uint2(0xc222221f,0x00000007),
 uint2(0x00000000,0x00000000),
 uint2(0x20820820,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0x00000000,0x00000000),
 uint2(0xc0000000,0x00000007)
};
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
 const uint cell=6*scale;
 if(id.x>=count*cell+2*scale||id.y>=9*scale)return;
 float4 color=float4(0,0,0,1);
 if(id.x>=scale&&id.y>=scale){
  const uint tx=id.x-scale,ty=id.y-scale,ci=tx/cell,col=(tx%cell)/scale,row=ty/scale;
  if(ci<count&&col<5&&row<7){
   const uint word=text[ci/16][(ci/4)%4],ch=(word>>((ci%4)*8))&255u;
   const uint g=(ch>=32&&ch<96)?ch-32:0;const uint bit=row*5+col;
   const bool on=bit<32?((FONT[g].x>>bit)&1u)!=0:((FONT[g].y>>(bit-32))&1u)!=0;
   if(on)color=float4(1,0.85,0.2,1);
  }
 }
 Store(id.xy,color);
}
