ByteAddressBuffer w : register(t0);
StructuredBuffer<float> inp : register(t1);
StructuredBuffer<float> feat_in : register(t2);
StructuredBuffer<float> qkv_in : register(t3);
RWStructuredBuffer<float> feat : register(u0);
RWStructuredBuffer<float> outp : register(u1);
RWStructuredBuffer<float> qkv_out : register(u2);

static const uint width = WIDTH, height = HEIGHT, shifted = SHIFTED;
float W(uint i) { return asfloat(w.Load(i * 4)); }
uint linear_id(uint3 g, uint3 t) { return (g.y * 65535 + g.x) * 64 + t.x; }
float fp8(float x) {
    if (x == 0) return 0;
    float s = x < 0 ? -1 : 1, a = abs(x);
    if (a < 0.015625) return s * round(a * 512.0) / 512.0;
    float e = clamp(floor(log2(a)), -6.0, 8.0);
    float m = round((a / exp2(e) - 1.0) * 8.0);
    if (m >= 8) { m = 0; e += 1; }
    return s * min(exp2(e) * (1.0 + m / 8.0), 448.0);
}
void load_qkv(uint token, out float q[16], out float k[16], out float v[16]) {
    [unroll] for (uint i = 0; i < 16; ++i) {
        q[i] = qkv_in[token * 48 + i];
        k[i] = qkv_in[token * 48 + 16 + i];
        v[i] = qkv_in[token * 48 + 32 + i];
    }
}
uint region(uint p, uint n) { return p < n - 8 ? 0 : (p < n - 4 ? 1 : 2); }

[numthreads(64, 1, 1)]
void attention(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
    uint t = linear_id(group, thread), tokens = width * height;
    if (t >= tokens) return;
    uint ox = t % width, oy = t / width;
    uint rx = shifted ? (ox + width - 4) % width : ox;
    uint ry = shifted ? (oy + height - 4) % height : oy;
    uint qx = rx % 8, qy = ry % 8, query = qy * 8 + qx;
    uint wx = rx - qx, wy = ry - qy;
    float qv[16], unused_k[16], unused_v[16];
    load_qkv(t, qv, unused_k, unused_v);
    float mx = -3.4e38;
    [loop] for (uint key = 0; key < 64; ++key) {
        uint kx = wx + key % 8, ky = wy + key / 8;
        bool allow = !shifted || (region(rx, width) == region(kx, width) && region(ry, height) == region(ky, height));
        uint sx = shifted ? (kx + 4) % width : kx, sy = shifted ? (ky + 4) % height : ky;
        float tq[16], tk[16], tv[16]; load_qkv(sy * width + sx, tq, tk, tv);
        float qq = 0, kk = 0, dot = 0;
        [unroll] for (uint i = 0; i < 16; ++i) { qq += qv[i] * qv[i]; kk += tk[i] * tk[i]; dot += qv[i] * tk[i]; }
        float z = dot * rsqrt(max(qq, 1e-12)) * rsqrt(max(kk, 1e-12)) * W(10240) + W(6144 + query * 64 + key);
        if (allow) mx = max(mx, z);
    }
    float den = 0, acc[16]; [unroll] for (uint i = 0; i < 16; ++i) acc[i] = 0;
    [loop] for (uint key = 0; key < 64; ++key) {
        uint kx = wx + key % 8, ky = wy + key / 8;
        bool allow = !shifted || (region(rx, width) == region(kx, width) && region(ry, height) == region(ky, height));
        if (!allow) continue;
        uint sx = shifted ? (kx + 4) % width : kx, sy = shifted ? (ky + 4) % height : ky;
        float tq[16], tk[16], tv[16]; load_qkv(sy * width + sx, tq, tk, tv);
        float qq = 0, kk = 0, dot = 0;
        [unroll] for (uint i = 0; i < 16; ++i) { qq += qv[i] * qv[i]; kk += tk[i] * tk[i]; dot += qv[i] * tk[i]; }
        float e = exp(dot * rsqrt(max(qq, 1e-12)) * rsqrt(max(kk, 1e-12)) * W(10240) + W(6144 + query * 64 + key) - mx);
        den += e; [unroll] for (uint i = 0; i < 16; ++i) acc[i] += e * tv[i];
    }
    [loop] for (uint c = 0; c < 32; ++c) {
        float z = 0; [unroll] for (uint i = 0; i < 16; ++i) z += (acc[i] / den) * W(5632 + c * 16 + i);
        outp[t * 32 + c] = fp8(z + feat_in[t * 32 + c] * W(10273 + c));
    }
}
