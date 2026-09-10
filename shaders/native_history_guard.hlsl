// History guard (DLSS5_HISTORY_GUARD=<dark/255>,<bright/255>): in place on the motion-warped history the network reads.
// A history pixel darker than `dark` whose current input (post_base, the same working-surface encoding) is brighter than
// `bright` is replaced by the input: the network's own no-history convention (preblock_input_mix fills the history
// features with the input colour), applied per pixel. Stops the temporal branch from reproducing a black history tile.
StructuredBuffer<float4> base : register(t0);
RWStructuredBuffer<float4> warped : register(u0);
cbuffer Params : register(b0) { float dark; float bright; uint pixels; uint pad; }
[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint p = id.x; if (p >= pixels) return;
    float4 h = warped[p]; float3 b = base[p].xyz;
    if (max(max(h.x, h.y), h.z) < dark && max(max(b.x, b.y), b.z) > bright) warped[p] = float4(b, h.w);
}
