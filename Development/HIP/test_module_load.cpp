// Requires the HIP runtime; module operations are injected so failures are deterministic.
#include "hip_api.h"
#include <iostream>

namespace {
int loads, unloads, copies, loadError, copyError, globalError;
float deviceStyle;
hip_probe::Handle token = &deviceStyle;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int Load(hip_probe::Handle* out, const void*) { ++loads; if (!loadError) *out = token; return loadError; }
int Unload(hip_probe::Handle h) { Require(h == token, "unexpected unload handle"); ++unloads; return 0; }
int Global(void** out, size_t* size, hip_probe::Handle h, const char*) {
    Require(h == token, "unexpected global handle"); *out = &deviceStyle; *size = sizeof(float); return globalError;
}
int Copy(void*, const void*, size_t, int) { ++copies; return copyError; }
const char* Error(int) { return "injected failure"; }
}

int main(int argc, char** argv) {
    try {
        Require(argc == 2, "expected a readable nonempty fixture file");
        hip_probe::Api api;
        api.hipModuleLoadData = Load; api.hipModuleUnload = Unload;
        api.hipModuleGetGlobal = Global; api.hipMemcpy = Copy; api.hipGetErrorName = Error;
        api.style_feature = 0;
        hip_probe::Handle module = nullptr;
        copyError = 1;
        bool threw = false;
        try { api.LoadModule(&module, argv[1]); } catch (const std::runtime_error&) { threw = true; }
        Require(threw && module == nullptr && loads == 1 && unloads == 1 && copies == 1,
                "failed Style initialization leaked or published a module");

        // A missing optional API export throws before the device copy, but still owns a module.
        api.hipModuleGetGlobal = nullptr;
        auto savedDll = api.dll; api.dll = GetModuleHandleW(L"kernel32.dll");
        threw = false;
        try { api.LoadModule(&module, argv[1]); } catch (const std::runtime_error&) { threw = true; }
        api.dll = savedDll; api.hipModuleGetGlobal = Global;
        Require(threw && module == nullptr && loads == 2 && unloads == 2 && copies == 1,
                "missing HIP export leaked a module");

        copyError = 0;
        Require(api.LoadModule(&module, argv[1]) == 0 && module == token && unloads == 2 && copies == 2,
                "successful initialization did not transfer ownership");
        api.hipModuleUnload(module);
        loadError = 7;
        Require(api.LoadModule(&module, argv[1]) == 7 && module == nullptr && unloads == 3 && copies == 2,
                "load error initialized or unloaded an unowned module");

        loadError = 0; globalError = 1;
        Require(api.LoadModule(&module, argv[1]) == 0 && module == token && copies == 2,
                "modules without a Style constant must remain supported");
        api.hipModuleUnload(module);
        globalError = 0; api.style_feature = -1;
        Require(api.LoadModule(&module, argv[1]) == 0 && module == token && copies == 2,
                "default Style unexpectedly wrote device memory");
        api.hipModuleUnload(module);
        Require(loads == 6 && unloads == 5, "module ownership imbalance");
        std::cout << "PASS module initialization ownership and failure cleanup\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
