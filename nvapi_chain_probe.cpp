#define WIN32_LEAN_AND_MEAN
#include "MinHook.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <d3d12.h>
#include <string>
#include <vector>
#include <windows.h>
namespace {
using Handle = void *;
HMODULE self = nullptr;
struct D3 {
  uint32_t x, y, z;
};
struct K {
  Handle fn;
  D3 grid, block;
  uint32_t shared;
  const void *params;
  uint32_t size;
  void **kernelParams;
};
using Query = void *(__cdecl *)(unsigned);
using CreateFn = int(__cdecl *)(ID3D12Device *, Handle, const char *, Handle *);
using Launch = int(__cdecl *)(ID3D12GraphicsCommandList *, const K *, uint32_t);
CreateFn originalCreate = nullptr;
Launch originalLaunch = nullptr;
SRWLOCK lock = SRWLOCK_INIT;
std::vector<std::pair<Handle, std::string>> names;
std::atomic<unsigned> launches{0};
constexpr wchar_t path[] = L"D:\\DLSSNR-Lab\\logs\\nvapi-chain.txt";
void log(const char *fmt, ...) {
  AcquireSRWLockExclusive(&lock);
  FILE *f = _wfopen(path, L"ab");
  if (f) {
    va_list a;
    va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fclose(f);
  }
  ReleaseSRWLockExclusive(&lock);
}
std::string nameOf(Handle h) {
  AcquireSRWLockShared(&lock);
  std::string result = "?";
  for (const auto &i : names) {
    if (i.first == h) {
      result = i.second;
      break;
    }
  }
  ReleaseSRWLockShared(&lock);
  return result;
}
int hookCreate(ID3D12Device *d, Handle m, const char *n, Handle *out) {
  int r = originalCreate(d, m, n, out);
  if (!r && out) {
    AcquireSRWLockExclusive(&lock);
    names.emplace_back(*out, n ? n : "");
    ReleaseSRWLockExclusive(&lock);
    log("CREATE handle=%p name=%s\n", *out, n ? n : "");
  }
  return r;
}
int hookLaunch(ID3D12GraphicsCommandList *c, const K *k, uint32_t n) {
  unsigned call = launches.fetch_add(1);
  if (call < 512 && k) {
    log("LAUNCH call=%u count=%u\n", call, n);
    for (uint32_t i = 0; i < n && i < 64; i++) {
      const std::string name = nameOf(k[i].fn);
      log("  K%u handle=%p name=%s grid=%u,%u,%u block=%u,%u,%u bytes=%u "
          "params=%p\n",
          i, k[i].fn, name.c_str(), k[i].grid.x, k[i].grid.y, k[i].grid.z,
          k[i].block.x, k[i].block.y, k[i].block.z, k[i].size, k[i].params);
      if (k[i].params && k[i].size <= 0x100) {
        unsigned char b[0x100]{};
        SIZE_T got = 0;
        if (ReadProcessMemory(GetCurrentProcess(), k[i].params, b, k[i].size,
                              &got) &&
            got == k[i].size) {
          log("    DATA");
          for (uint32_t j = 0; j < k[i].size; j++)
            log(" %02x", b[j]);
          log("\n");
        }
      }
    }
  }
  return originalLaunch(c, k, n);
}
DWORD WINAPI worker(void *) {
  HMODULE n = nullptr;
  for (int i = 0; i < 600 && !n; i++) {
    n = GetModuleHandleW(L"nvapi64.dll");
    if (!n)
      Sleep(100);
  }
  if (!n)
    return 1;
  auto q = (Query)GetProcAddress(n, "nvapi_QueryInterface");
  if (!q || MH_Initialize() != MH_OK)
    return 2;
  void *cf = q(0xe2436e22), *lf = q(0x846a9bf0);
  if (!cf || !lf)
    return 3;
  if (MH_CreateHook(cf, (void *)&hookCreate, (void **)&originalCreate) != MH_OK)
    return 4;
  if (MH_CreateHook(lf, (void *)&hookLaunch, (void **)&originalLaunch) != MH_OK)
    return 5;
  DeleteFileW(path);
  return MH_EnableHook(MH_ALL_HOOKS) == MH_OK ? 0 : 6;
}
} // namespace
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
  if (r == DLL_PROCESS_ATTACH) {
    self = h;
    DisableThreadLibraryCalls(h);
    // ReShade may unregister add-ons while recreating the D3D12 device during
    // startup. The worker must not continue executing from an unloaded image.
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&worker), &pinned);
    HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (t)
      CloseHandle(t);
  }
  return TRUE;
}
