#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "LmxxfProductionOptions.h"
#include <cstdio>
#include <cstdint>

using namespace hip_reference;
#if !defined(TEST_LEGACY_BASELINE) && !defined(HIP_MP_RAW_EXPORT)
// Inspect the actual intermediate/final post dispatch without adding a public test API.
namespace hip_reference {
template<class Tag, typename Tag::type Member> struct AuxAccess {
    friend typename Tag::type Access(Tag) { return Member; }
};
struct GraphMember {
    using type = Tensor(Network::*)(Tensor, Tensor, Tensor, bool, void*, bool);
    friend type Access(GraphMember);
};
template struct AuxAccess<GraphMember, &Network::RunGraph>;
}
#endif

static void Require(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
static uint64_t Hash(const std::vector<float>& values) {
    uint64_t hash = 14695981039346656037ull;
    const auto* bytes = reinterpret_cast<const unsigned char*>(values.data());
    for (size_t i = 0; i < values.size() * sizeof(float); ++i)
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

int main(int argc, char** argv) try {
    Require(argc == 3, "usage: test_multipass_aux ASSETS ARCH_MODULE_DIRECTORY");
    _putenv_s("DLSS5_MULTI_PASS", "3");
    _putenv_s("DLSS5_MULTI_PASS_PREDICT", "0");
    _putenv_s("DLSS5_MULTI_PASS_SKIN_PROTECT", "0");
    _putenv_s("DLSS5_MULTI_PASS_SKIP_BLOCKS", "");
    _putenv_s("DLSS5_FAST_NUMERIC", "1");
    _putenv_s("DLSS5_SKIP_BLOCKS", "");
    _putenv_s("DLSS5_HIP_GRAPH", "0");
    _putenv_s("DLSS5_OVERLAP", "0");
    _putenv_s("DLSS5_VIT_ADAPTIVE", "0");
    const U w = 1280, h = 768;
    const size_t pixels = size_t(w) * h;
    Api probe(7);
    probe.Check(probe.hipInit(0), "HIP init");
    int count = 0, device = -1;
    probe.Check(probe.hipGetDeviceCount(&count), "device count");
    for (int i = 0; i < count; ++i) {
        std::string arch(probe.Properties(i).gcnArchName);
        arch = arch.substr(0, arch.find(':'));
        if (arch == "gfx1200" || arch == "gfx1201") { device = i; break; }
    }
    Require(device >= 0, "RDNA4 device required");
    probe.Check(probe.hipSetDevice(device), "device");
    auto options = LmxxfProductionOptions(w, h, argv[2], argv[1]);
    options.device = device;
    options.submit_pulse = 0;
    options.experimental_temporal = options.temporal_feature_tap = false;
    options.integration.allow_vit_hotkey = options.integration.allow_input_poll = false;
    Network n(options);
    n.SetNoise({});
    n.SetAdaptiveReuseAllowed(false);
    auto& api = n.Runtime();
    auto input = std::make_shared<Allocation>(api, pixels * 16);
    auto history = std::make_shared<Allocation>(api, pixels * 16);
    auto output = std::make_shared<Allocation>(api, pixels * 12);
    auto auxiliary = std::make_shared<Allocation>(api, pixels * 8);
    std::vector<float> image(pixels * 4, .4f), previous(pixels * 4, .3f);
    std::vector<float> sentinel(pixels * 2, 123456.f), read(sentinel.size());
    std::vector<float> rgb(pixels * 3);
    for (size_t i = 0; i < pixels; ++i) {
        image[i * 4] = .2f + .3f * float(i % w) / w;
        previous[i * 4 + 1] = .1f + .2f * float(i / w) / h;
        image[i * 4 + 3] = previous[i * 4 + 3] = 1;
    }
    api.Check(api.hipMemcpy(input->ptr, image.data(), input->bytes, 1), "input");
    api.Check(api.hipMemcpy(history->ptr, previous.data(), history->bytes, 1), "history");
    auto resetAux = [&] {
        api.Check(api.hipMemcpy(auxiliary->ptr, sentinel.data(), auxiliary->bytes, 1), "sentinel");
    };
    auto readAux = [&] {
        api.Check(api.hipMemcpy(read.data(), auxiliary->ptr, auxiliary->bytes, 2), "read auxiliary");
    };
    auto checkAux = [&] {
        readAux();
        for (float x : read) Require(std::isfinite(x) && x != 123456.f, "all final auxiliary pixels written and finite");
    };
    // Includes returning to MP1 and changing prediction/skin mode on the same instance.
    struct Mode { U passes; bool prediction, skin; };
    const Mode modes[] = {{1,false,false}, {2,false,false}, {3,false,false},
                          {3,true,false}, {3,true,true}, {2,false,true}, {1,false,false}};
    auto run = [&](const Mode& mode, bool aux) {
        n.SetMultiPass(mode.passes);
        n.SetMultiPassPredict(mode.prediction);
        n.SetMultiPassSkinProtect(mode.skin);
        resetAux();
        n.Enqueue(input->ptr, history->ptr, output->ptr, 17);
        n.Synchronize();
        api.Check(api.hipMemcpy(rgb.data(), output->ptr, output->bytes, 2), "read RGB");
        for (float x : rgb) Require(std::isfinite(x), "finite RGB");
        if (aux) checkAux();
        else { readAux(); Require(read == sentinel, "unrequested auxiliary remains untouched"); }
        std::printf("%s mp=%u prediction=%u skin=%u rgb=%016llx\n", aux ? "AUX" : "LEGACY",
                    mode.passes, unsigned(mode.prediction), unsigned(mode.skin),
                    static_cast<unsigned long long>(Hash(rgb)));
        return rgb;
    };
    for (const auto& mode : modes) run(mode, false);
#ifdef HIP_MP_RAW_EXPORT
    Require(!n.NativeHistorySupported(), "raw-export diagnostic must reject auxiliary history");
    bool rejected = false;
    try {
        n.EnableNativePostHistory(std::vector<float>(32, .03125f),
                                  {auxiliary->ptr, auxiliary->bytes, w, h, 8});
    } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected, "raw-export auxiliary request must fail explicitly");
    std::puts("PASS: raw-export diagnostic rejects unsupported auxiliary schedule");
#elif !defined(TEST_LEGACY_BASELINE)
    // Synthetic row checks routing/storage only; no model coefficients are distributed.
    n.EnableNativePostHistory(std::vector<float>(32, .03125f),
                              {auxiliary->ptr, auxiliary->bytes, w, h, 8});
    const auto graph = Access(GraphMember{});
    n.SetMultiPass(3);
    for (bool rgba : {false, true}) {
        resetAux();
        auto intermediate = (n.*graph)(input, {}, {}, rgba, nullptr, false);
        n.Synchronize();
        readAux();
        Require(read == sentinel, "intermediate RGB/RGBA pass must not write auxiliary");
    }
    for (const auto& mode : modes) {
        const auto first = run(mode, true);
        const auto replay = run(mode, true);
        Require(first == replay, "identical auxiliary frame replay must be deterministic");
    }
    std::puts("PASS: final-only auxiliary, MP1/2/3, prediction/skin, live switches and replay");
#else
    std::puts("PASS: legacy baseline output capture");
#endif
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
