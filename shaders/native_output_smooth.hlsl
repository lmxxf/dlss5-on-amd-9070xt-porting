// Output-side temporal smoothing (DLSS5_OUTPUT_SMOOTH=<threshold/255>,<strength>). In place on the network RGB output:
// where the output differs little from the motion-warped previous output, blend toward it; large differences pass through.
// Suppresses the few-/255 halo shimmer around jittering edges without touching real motion. Runs before the history copy,
// so the blend is recursive (IIR); strength <= 0.8 keeps the lag short.
StructuredBuffer<float4> warped : register(t0);
RWStructuredBuffer<float> rgb : register(u0);
cbuffer Params : register(b0) { float threshold; float strength; uint pixels; uint pad; }
[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint p = id.x; if (p >= pixels) return;
    float4 h = warped[p]; if (h.w <= 0) return;
    float3 o = float3(rgb[p*3], rgb[p*3+1], rgb[p*3+2]);
    float d = max(max(abs(o.x - h.x), abs(o.y - h.y)), abs(o.z - h.z));
    float w = strength * saturate(1.0 - d / threshold);
    o = lerp(o, h.xyz, w);
    rgb[p*3] = o.x; rgb[p*3+1] = o.y; rgb[p*3+2] = o.z;
}
