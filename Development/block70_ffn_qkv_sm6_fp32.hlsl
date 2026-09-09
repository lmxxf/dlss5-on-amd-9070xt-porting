#define WIDTH 3840
#define HEIGHT 2160
ByteAddressBuffer weights : register(t0);
StructuredBuffer<float> input : register(t1), feature_unused : register(t2), qkv_unused : register(t3);
RWStructuredBuffer<float> features : register(u0), output_unused : register(u1), qkv : register(u2);

float weight(uint i) { return asfloat(weights.Load(i * 4)); }
uint input_index(uint t, uint c) {
    uint x = t % WIDTH, y = t / WIDTH;
    uint tile = (y / 8) * (WIDTH / 8) + x / 8;
    uint local = ((y % 8) * 8 + x % 8) * 32 + c;
    return tile * 2048 + local;
}
float fp8(float x) {
    if (x == 0) return 0;
    float s = x < 0 ? -1 : 1, a = abs(x);
    if (a < .015625) return s * round(a * 512) / 512;
    float e = clamp(floor(log2(a)), -6., 8.);
    float m = round((a / exp2(e) - 1) * 8);
    if (m >= 8) { m = 0; e += 1; }
    return s * min(exp2(e) * (1 + m / 8), 448.);
}

[numthreads(64, 1, 1)]
void ffn_qkv(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
    uint t = (group.y * 65535 + group.x) * 64 + thread.x;
    if (t >= WIDTH * HEIGHT) return;
    float hidden[64], local_feature[32];
    [unroll] for (uint j = 0; j < 64; ++j) {
        float a = 0;
        [unroll] for (uint c = 0; c < 32; ++c)
            a += input[input_index(t, c)] * weight(j * 32 + c);
        a = clamp(a, -4., 4.);
        hidden[j] = a * (.89453125 + a * (.447265625 - .055908203125 * abs(a)));
    }
    [unroll] for (uint c = 0; c < 32; ++c) {
        float v = 0;
        [unroll] for (uint j = 0; j < 64; ++j) v += hidden[j] * weight(2048 + c * 64 + j);
        v = fp8(v + input[input_index(t, c)] * weight(10241 + c));
        local_feature[c] = v;
        features[t * 32 + c] = v;
    }
    [unroll] for (uint o = 0; o < 16; ++o) {
        float q = 0, k = 0, v = 0;
        [unroll] for (uint j = 0; j < 16; ++j) {
            float even = local_feature[j * 2], odd = local_feature[j * 2 + 1];
            q += even * weight(4096 + o * 16 + j) + odd * weight(4352 + o * 16 + j);
            k += even * weight(4608 + o * 16 + j) + odd * weight(4864 + o * 16 + j);
            v += even * weight(5120 + o * 16 + j) + odd * weight(5376 + o * 16 + j);
        }
        qkv[t * 48 + o] = q;
        qkv[t * 48 + 16 + o] = k;
        qkv[t * 48 + 32 + o] = v;
    }
}
