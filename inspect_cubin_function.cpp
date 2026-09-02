#include <cuda.h>
#include <cstdio>
#include <cstdlib>

static void check(const char *name, CUresult result) {
    if (result != CUDA_SUCCESS) {
        const char *text = nullptr; cuGetErrorString(result, &text);
        std::fprintf(stderr, "%s=%d %s\n", name, result, text ? text : "?");
        std::exit(1);
    }
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    check("init", cuInit(0)); CUdevice device; check("device", cuDeviceGet(&device, 0));
    CUcontext context; check("context", cuDevicePrimaryCtxRetain(&context, device));
    check("current", cuCtxSetCurrent(context)); CUmodule module;
    check("module", cuModuleLoad(&module, argv[1])); CUfunction function;
    check("function", cuModuleGetFunction(&function, module, argv[2]));
    const struct { CUfunction_attribute attribute; const char *name; } items[] = {
        {CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, "max_threads"},
        {CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, "shared_bytes"},
        {CU_FUNC_ATTRIBUTE_NUM_REGS, "registers"},
        {CU_FUNC_ATTRIBUTE_CLUSTER_SIZE_MUST_BE_SET, "cluster_must_be_set"},
        {CU_FUNC_ATTRIBUTE_REQUIRED_CLUSTER_WIDTH, "required_cluster_width"},
        {CU_FUNC_ATTRIBUTE_REQUIRED_CLUSTER_HEIGHT, "required_cluster_height"},
        {CU_FUNC_ATTRIBUTE_REQUIRED_CLUSTER_DEPTH, "required_cluster_depth"},
        {CU_FUNC_ATTRIBUTE_NON_PORTABLE_CLUSTER_SIZE_ALLOWED, "nonportable_cluster"},
        {CU_FUNC_ATTRIBUTE_CLUSTER_SCHEDULING_POLICY_PREFERENCE, "cluster_policy"},
    };
    for (const auto &item : items) {
        int value = 0; CUresult result = cuFuncGetAttribute(&value, item.attribute, function);
        std::printf("%s result=%d value=%d\n", item.name, result, value);
    }
}
