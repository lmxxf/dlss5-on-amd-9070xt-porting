// Candidate game boundary: exact-size RGB texture, no resize or color transform.
// Host must validate a single-sample 1920x1080 float-compatible SRV and synchronize
// texture ownership before dispatch. Padding matches the controlled RGB contract;
// actual game color-space and optional texture contracts require separate proof.
Texture2D<float4> source : register(t0);
RWStructuredBuffer<float4> tiles : register(u0);
RWStructuredBuffer<float4> post_base : register(u1);

[numthreads(8,8,1)]
void main(uint3 group : SV_GroupID, uint3 lane : SV_GroupThreadID) {
    uint2 p = group.xy * 8 + lane.xy;
    if (p.x >= 1920 || p.y >= 1152) return;
    uint sy = p.y < 1080 ? p.y : 2158 - p.y;
    float4 pixel = source.Load(int3(p.x, sy, 0));
    uint tile = group.y * 240 + group.x;
    tiles[tile * 64 + lane.y * 8 + lane.x] = pixel;
    post_base[p.y * 1920 + p.x] = pixel;
}
