// Candidate reconstruction of captured 5090 mode1 input codec.
// Independent oracle: codec-22724-36e36c050.dxbc. Not yet game-integrated.
Texture2D<float4> Original : register(t0);
RWTexture2D<float4> Output : register(u0);
cbuffer CodecConstants : register(b0) {
    uint2 Size;
    uint2 SourceSize;
    uint2 SourceBase;
    uint2 ProxySize;
    float PaperWhiteScale;
    float TransferStrength;
    float ColorStrength;
    uint HdrMode;
    float4 Padding;
};
#ifndef NATIVE_CODEC_SRGB_IO
#define NATIVE_CODEC_SRGB_IO 0
#endif
[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= Size)) return;
#if NATIVE_CODEC_SRGB_IO
    // DLSS5_CODEC_SRGB (Magpie): the source is already a display-referred sRGB picture, i.e. already in the network's working
    // surface encoding; no paper-white scale, shoulder or transfer curve (applying them again double-encodes: the "whiter" look).
    {
        uint2 extent = max(SourceSize, uint2(1,1));
        uint2 p = SourceBase + min(uint2((float2(id.xy)+0.5)*float2(extent)/float2(Size)), extent-1);
        Output[id.xy] = float4(saturate(Original.Load(int3(p,0)).rgb),1);
        return;
    }
#endif
    // Only the captured mode1 contract is implemented here. Host must reject
    // other modes before dispatch; zero output makes accidental misuse visible.
    if (HdrMode != 1 || PaperWhiteScale <= 0) {
        Output[id.xy] = 0;
        return;
    }
    uint2 extent = max(SourceSize, uint2(1,1));
    uint2 p = SourceBase + min(uint2((float2(id.xy)+0.5)*float2(extent)/float2(Size)), extent-1);
    float3 value = max(Original.Load(int3(p,0)).rgb,0) / PaperWhiteScale;
    float3 shoulder = 0.75 + 0.25 * (1.0 - exp(-5.770780 * (value-0.75)));
    value = saturate(value <= 0.75 ? value : shoulder);
    value = value <= 0.0031308 ? value*12.92 : 1.055*pow(value,1.0/2.4)-0.055;
    // FP16 rounding is performed by the RGBA16_FLOAT destination texture,
    // matching the reference working surface, not by the later RGB unpacker.
    Output[id.xy] = float4(value,1);
}
