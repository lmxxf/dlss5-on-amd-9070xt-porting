// Diagnostic (DLSS5_BLACK_PROBE): statistics of the network RGB residual, one dispatch per frame, atomics into a 4-uint buffer:
// x = non-finite count, y = count of |v| > 1.5 (a residual that large blacks out or saturates the pixel), z = max |v| as float bits
// (positive floats order like uints), w = sum |v| * 64 (fixed point, mean = w / 64 / values). 256 groups x 64 threads stride the buffer.
StructuredBuffer<float> rgb : register(t0);
RWByteAddressBuffer stats : register(u0);
cbuffer Params : register(b0) { uint values; uint pad0; uint pad1; uint pad2; }
[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint nonfinite = 0, big = 0, maxbits = 0; float sum = 0;
    for (uint i = id.x; i < values; i += 256 * 64) {
        float v = rgb[i];
        if (!isfinite(v)) { nonfinite++; continue; }
        float a = abs(v); if (a > 1.5) big++; maxbits = max(maxbits, asuint(a)); sum += a;
    }
    nonfinite = WaveActiveSum(nonfinite); big = WaveActiveSum(big); maxbits = WaveActiveMax(maxbits); sum = WaveActiveSum(sum);
    if (WaveIsFirstLane()) {
        uint old;
        if (nonfinite) stats.InterlockedAdd(0, nonfinite, old);
        if (big) stats.InterlockedAdd(4, big, old);
        stats.InterlockedMax(8, maxbits, old);
        stats.InterlockedAdd(12, uint(sum * 64.0), old);
    }
}
