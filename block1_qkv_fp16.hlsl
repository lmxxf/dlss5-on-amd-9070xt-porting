#ifndef WIDTH
#define WIDTH 1920
#endif
#ifndef HEIGHT
#define HEIGHT 1088
#endif

ByteAddressBuffer weights : register(t0);
StructuredBuffer<float> input_unused : register(t1);
StructuredBuffer<float> features : register(t2);
StructuredBuffer<float> qkv_unused : register(t3);
RWStructuredBuffer<float> feature_unused : register(u0);
RWStructuredBuffer<float> output_unused : register(u1);
RWStructuredBuffer<float> qkv : register(u2);

float16_t weight(uint index) {
    return (float16_t)asfloat(weights.Load(index * 4));
}

[numthreads(64, 1, 1)]
void qkv_precompute(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
    const uint token = (group.y * 65535 + group.x) * 64 + thread.x;
    if (token >= WIDTH * HEIGHT) return;
    [unroll]
    for (uint output = 0; output < 16; ++output) {
        float16_t q = 0, k = 0, v = 0;
        [unroll]
        for (uint pair = 0; pair < 16; ++pair) {
            const float16_t even = (float16_t)features[token * 32 + pair * 2];
            const float16_t odd = (float16_t)features[token * 32 + pair * 2 + 1];
            q += even * weight(4096 + output * 16 + pair)
               + odd * weight(4352 + output * 16 + pair);
            k += even * weight(4608 + output * 16 + pair)
               + odd * weight(4864 + output * 16 + pair);
            v += even * weight(5120 + output * 16 + pair)
               + odd * weight(5376 + output * 16 + pair);
        }
        qkv[token * 48 + output] = (float)q;
        qkv[token * 48 + 16 + output] = (float)k;
        qkv[token * 48 + 32 + output] = (float)v;
    }
}
