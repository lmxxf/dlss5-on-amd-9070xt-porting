#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

static std::vector<float> read_f32(const wchar_t *path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { std::fwprintf(stderr, L"cannot open %ls\n", path); ExitProcess(2); }
    const auto bytes = static_cast<size_t>(f.tellg());
    if (bytes % sizeof(float)) ExitProcess(2);
    f.seekg(0);
    std::vector<float> values(bytes / sizeof(float));
    f.read(reinterpret_cast<char *>(values.data()), bytes);
    if (!f) ExitProcess(2);
    return values;
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 7) {
        std::fwprintf(stderr, L"usage: %ls projected.f32 skip.f32 output.f32 height width channels\n", argv[0]);
        return 2;
    }
    const uint64_t height = _wtoi(argv[4]);
    const uint64_t width = _wtoi(argv[5]);
    const uint64_t channels = _wtoi(argv[6]);
    if (!height || !width || !channels) return 2;
    const auto projected = read_f32(argv[1]);
    const auto skip = read_f32(argv[2]);
    const uint64_t projected_count = height * width * channels;
    const uint64_t output_count = height * 2 * width * 2 * channels;
    if (projected.size() != projected_count || skip.size() != output_count) {
        std::fprintf(stderr, "bad shape: projected=%zu/%llu skip=%zu/%llu\n",
            projected.size(), static_cast<unsigned long long>(projected_count),
            skip.size(), static_cast<unsigned long long>(output_count));
        return 2;
    }
    std::vector<float> output(output_count);
    double sum = 0, sum2 = 0;
    bool finite = true;
    const uint64_t output_width = width * 2;
    for (uint64_t y = 0; y < height * 2; ++y) {
        for (uint64_t x = 0; x < output_width; ++x) {
            const uint64_t src = ((y / 2) * width + x / 2) * channels;
            const uint64_t dst = (y * output_width + x) * channels;
            for (uint64_t c = 0; c < channels; ++c) {
                const float value = projected[src + c] + skip[dst + c];
                output[dst + c] = value;
                finite &= std::isfinite(value);
                sum += value;
                sum2 += static_cast<double>(value) * value;
            }
        }
    }
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char *>(output.data()), output.size() * sizeof(float));
    if (!f) return 2;
    const double n = static_cast<double>(output.size());
    const double variance = sum2 / n - (sum / n) * (sum / n);
    std::printf("shape=%llux%llux%llu finite=%s std=%.7g\n",
        static_cast<unsigned long long>(height * 2),
        static_cast<unsigned long long>(width * 2),
        static_cast<unsigned long long>(channels), finite ? "true" : "false",
        std::sqrt(variance > 0 ? variance : 0));
    return finite ? 0 : 3;
}
