// Candidate SDR R10G10B10A2_UNORM transfer, NOT an HDR/tone-map conversion.
// Input: contiguous float RGB, 1920x1152. Output: 1920x1080 packed uint buffer.
// Host must verify the destination format/color space and queue synchronization.
// A 1920*4-byte row is already aligned to D3D12's 256-byte copy row pitch.
StructuredBuffer<float> rgb : register(t0);
RWStructuredBuffer<uint> packed : register(u0);

[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint pixel = id.x;
    if (pixel >= 1920 * 1080) return;
    float3 value = float3(rgb[pixel*3], rgb[pixel*3+1], rgb[pixel*3+2]);
    uint3 channel = uint3(floor(saturate(value) * 1023.0 + 0.5));
    packed[pixel] = channel.r | (channel.g << 10) | (channel.b << 20) | (3u << 30);
}
