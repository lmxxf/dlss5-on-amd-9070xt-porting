// Processing RGB1920x1152 -> valid RGBA16_FLOAT1920x1080.
// No tone mapping/normalization: output codec owns those operations.
StructuredBuffer<float> rgb : register(t0);
RWTexture2D<float4> output : register(u0);
[numthreads(16,16,1)]
void main(uint3 id:SV_DispatchThreadID) {
 if(id.x>=1920||id.y>=1080)return;
 uint p=(id.y*1920+id.x)*3;
 output[id.xy]=float4(rgb[p],rgb[p+1],rgb[p+2],1);
}
