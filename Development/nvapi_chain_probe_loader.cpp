#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" __declspec(dllexport) const char *NAME = "NVAPI CUBIN Chain Probe Loader";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Loads the resident NVAPI chain hook outside the ReShade add-on lifetime.";

namespace {
using RegisterAddon = BOOL (*)(void *, unsigned);
using UnregisterAddon = void (*)(void *);
HMODULE self = nullptr;
UnregisterAddon unregisterAddon = nullptr;
DWORD WINAPI loadHook(void *) {
  LoadLibraryW(L"nvapi-chain-hook.dll");
  FreeLibraryAndExitThread(self, 0);
}
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    self = h;
    DisableThreadLibraryCalls(h);
    HMODULE reshade = GetModuleHandleW(L"d3d12.dll");
    auto reg = reshade ? reinterpret_cast<RegisterAddon>(
                             GetProcAddress(reshade, "ReShadeRegisterAddon"))
                       : nullptr;
    unregisterAddon = reshade ? reinterpret_cast<UnregisterAddon>(
                                    GetProcAddress(reshade, "ReShadeUnregisterAddon"))
                              : nullptr;
    if (!reg || !unregisterAddon || !reg(h, 18))
      return FALSE;
    // Hold one temporary reference so ReShade can unload this add-on while the
    // loader thread is starting; FreeLibraryAndExitThread releases it safely.
    HMODULE held = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(&loadHook), &held)) {
      HANDLE thread = CreateThread(nullptr, 0, loadHook, nullptr, 0, nullptr);
      if (thread)
        CloseHandle(thread);
      else
        FreeLibrary(held);
    }
  } else if (reason == DLL_PROCESS_DETACH && unregisterAddon) {
    unregisterAddon(self);
  }
  return TRUE;
}
