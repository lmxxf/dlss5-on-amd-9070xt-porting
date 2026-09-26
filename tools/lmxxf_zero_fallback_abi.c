#include "LmxxfNrApi.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int32_t (*GetApiFn)(uint32_t, LmxxfNrApi *);

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void)
{
    HMODULE dll = LoadLibraryW(L"LmxxfNrRuntime.dll");
    if (!dll)
        return fail("runtime DLL did not load");
    GetApiFn getApi = (GetApiFn)(void *)GetProcAddress(dll, "LmxxfNrGetApi");
    if (!getApi)
        return fail("LmxxfNrGetApi export missing");

    LmxxfNrApi api = {0};
    api.struct_size = sizeof api;
    if (getApi(LMXXF_NR_ABI_VERSION, &api) != LMXXF_NR_OK || !api.Create ||
        !api.QueryCapabilities || !api.GetLastError)
        return fail("API table unavailable");

    LmxxfNrCapabilities caps = {0};
    caps.struct_size = sizeof caps;
    if (api.QueryCapabilities(&caps) != LMXXF_NR_OK ||
        caps.abi_version != LMXXF_NR_ABI_VERSION)
        return fail("capabilities query failed");

    LmxxfNrCreateInfo info = {0};
    info.struct_size = sizeof info;
    info.device = (void *)1;
    info.queue = (void *)1;
    info.assets_directory = L"";
    void *context = NULL;
    char diagnostic[256] = {0};

    info.flags = 1u << 31;
    if (api.Create(&info, &context) != LMXXF_NR_INVALID_ARGUMENT || context)
        return fail("unknown create flag was accepted");
    api.GetLastError(diagnostic, sizeof diagnostic);
    if (!strstr(diagnostic, "unknown flags"))
        return fail("unknown flag diagnostic missing");

    info.flags = LMXXF_NR_CREATE_FLAG_ZERO_OUTPUT_FALLBACK;
    memset(diagnostic, 0, sizeof diagnostic);
    if (api.Create(&info, &context) != LMXXF_NR_INVALID_ARGUMENT || context)
        return fail("invalid asset path was accepted");
    api.GetLastError(diagnostic, sizeof diagnostic);
    if (!strstr(diagnostic, "assets_directory required"))
        return fail("recovery flag was rejected before asset validation");

    FreeLibrary(dll);
    puts("LMXXF_NR_API_SMOKE_OK");
    return 0;
}
