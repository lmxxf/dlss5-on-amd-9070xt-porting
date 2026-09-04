#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#define DML_TARGET_VERSION_USE_LATEST
#ifndef _Maybenull_
#define _Maybenull_
#endif
#include <DirectML.h>
#include "reshade.hpp"
#include "MinHook.h"
struct DmlFailure { const char *operation; HRESULT result; };
#define DMLRT_FAILURE(name, result) throw DmlFailure{name, result}
#include "directml_gemm_runtime.h"
#include "swin64_1080_runtime.h"
#include "swin128_1080_runtime.h"
#include "swin256_1080_runtime.h"
#include "fp16_bridge_runtime.h"

extern "C" __declspec(dllexport) const char *NAME = "DLSS5 AMD 1080p Runtime";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Initializes a persistent DirectML operator on the game's D3D12 device.";

namespace {
constexpr wchar_t kLog[] = LR"(D:\DLSSNR-Lab\logs\dlss5-1080p-runtime.txt)";
static const GUID kDmlDevice = {0x6dbd6437, 0x96fd, 0x423f,
    {0xa9, 0x8c, 0xae, 0x5e, 0x7c, 0x2a, 0x57, 0x3f}};
static const GUID kDmlRecorder = {0xe6857a76, 0x2e3e, 0x4fdd,
    {0xbf, 0xf4, 0x5d, 0x2b, 0xa1, 0x0f, 0xb4, 0x53}};
using CreateDml = HRESULT(WINAPI *)(ID3D12Device *, DML_CREATE_DEVICE_FLAGS,
                                    REFIID, void **);

SRWLOCK g_log_lock = SRWLOCK_INIT;
std::atomic<bool> g_started{false}, g_ready{false}, g_failed{false};
std::atomic<unsigned long long> g_presents{0};
ID3D12Device *g_device = nullptr;
IDMLDevice *g_dml = nullptr;
IDMLCommandRecorder *g_recorder = nullptr;
ID3D12CommandQueue *g_queue = nullptr;
ID3D12CommandAllocator *g_allocator = nullptr;
ID3D12GraphicsCommandList *g_list = nullptr;
ID3D12Fence *g_fence = nullptr;
HANDLE g_event = nullptr;
DmlGemmOperator *g_operator = nullptr;
ID3D12Resource *g_probe_input = nullptr, *g_probe_weight = nullptr,
                 *g_probe_output = nullptr;
double g_probe_gpu_ms = 0.0;
std::atomic<unsigned long long> g_ffx_frames{0};
std::atomic<bool> g_ffx_hook_ready{false}, g_frame_contract_ready{false};
std::atomic<bool> g_frame_bridge_ready{false};
std::atomic<unsigned long long> g_frame_bridge_submissions{0};
ID3D12RootSignature *g_frame_bridge_root = nullptr;
ID3D12PipelineState *g_frame_bridge_pso = nullptr;
ID3D12DescriptorHeap *g_frame_bridge_heaps[8]{};
ID3D12Resource *g_frame_tiles = nullptr;
DmlGemmOperator *g_block0_l1 = nullptr, *g_block0_l2 = nullptr, *g_block0_l3 = nullptr;
ID3D12Resource *g_block0_packed = nullptr, *g_block0_raw1 = nullptr, *g_block0_hidden1 = nullptr,
                 *g_block0_raw2 = nullptr, *g_block0_hidden2 = nullptr, *g_block0_raw3 = nullptr,
                 *g_block0_tile = nullptr, *g_block0_hwc = nullptr;
ID3D12RootSignature *g_block0_root = nullptr;
ID3D12PipelineState *g_block0_pso[5]{};
ID3D12DescriptorHeap *g_block0_heap = nullptr;
std::atomic<bool> g_block0_ready{false};
std::atomic<unsigned long long> g_block0_submissions{0};
ID3D12Resource *g_front_main[2]{}, *g_front_feature = nullptr, *g_front_qkv = nullptr,
                 *g_front_weights[4]{};
ID3D12RootSignature *g_front_root = nullptr;
ID3D12PipelineState *g_front_pso[4][3]{};
ID3D12DescriptorHeap *g_front_heap = nullptr;
std::atomic<bool> g_front_ready{false};
std::atomic<unsigned long long> g_front_submissions{0};
DmlGemmOperator g_s64_gate[4],g_s64_up[4],g_s64_project[4],g_s64_qkv_op[4],g_s64_attention_project[4];
ID3D12Resource *g_s64_main[2]{},*g_s64_gate_out=nullptr,*g_s64_up_out=nullptr,*g_s64_hidden=nullptr,
                 *g_s64_project_raw=nullptr,*g_s64_feature=nullptr,*g_s64_qkv_raw=nullptr,
                 *g_s64_qkv_float=nullptr,*g_s64_attention_float=nullptr,*g_s64_attention=nullptr,
                 *g_s64_attention_raw=nullptr,*g_s64_mid=nullptr,*g_s64_down=nullptr,*g_s64_enter=nullptr;
ID3D12Resource *g_s64_gw[4]{},*g_s64_uw[4]{},*g_s64_pw[4]{},*g_s64_qw[4]{},*g_s64_aw[4]{},
                 *g_s64_fs[4]{},*g_s64_as[4]{},*g_s64_bias[4]{},*g_s64_scale[4]{};
Swin64Predown1080Pass g_s64_predown;
Swin64Boundary1080Pass g_s64_boundary[4];
Swin64Window1080Pass g_s64_window[4];
std::atomic<bool> g_s64_ready{false};
std::atomic<unsigned long long> g_s64_submissions{0};
struct RuntimeS128 {
    DmlGemmOperator gate[6],up[6],project[6],qkv[6],attention_project[6];
    ID3D12Resource *main[2]{},*gate_out{},*up_out{},*hidden{},*project_raw{},*feature{},*qkv_raw{},*qkv_float{},*attention_float{},*attention{},*attention_raw{},*mid{},*input_fp32{},*down{},*enter{};
    ID3D12Resource *gw[6]{},*uw[6]{},*pw[6]{},*qw[6]{},*aw[6]{},*fs[6]{},*as[6]{},*bias[6]{},*scale[6]{};
    Fp16ToFp32RuntimePass bridge;Swin128Predown1080Pass predown;Swin128Boundary1080Pass boundary[6];Swin128Window1080Pass window[6];
} g_s128;
struct RuntimeS256 {
    DmlGemmOperator gate[8],up[8],project[8],qkv[8],attention_project[8];
    ID3D12Resource *main[2]{},*gate_out{},*up_out{},*hidden{},*project_raw{},*feature{},*qkv_raw{},*qkv_float{},*attention_float{},*attention{},*attention_raw{},*mid{},*input_fp32{},*down{},*enter{};
    ID3D12Resource *gw[8]{},*uw[8]{},*pw[8]{},*qw[8]{},*aw[8]{},*fs[8]{},*as[8]{},*bias[8]{},*scale[8]{};
    Fp16ToFp32RuntimePass bridge;Swin256Predown1080Pass predown;Swin256Boundary1080Pass boundary[8];Swin256Window1080Pass window[8];
} g_s256;
std::atomic<bool> g_s128_ready{false},g_s256_ready{false};
std::atomic<unsigned long long> g_s128_submissions{0},g_s256_submissions{0};

struct FfxHeader { uint64_t type; FfxHeader *pNext; };
struct FfxDimensions2D { uint32_t width, height; };
struct FfxFloat2 { float x, y; };
struct FfxResourceDesc { uint32_t type, format, width, height, depth, mipCount, flags, usage; };
struct FfxResource { void *resource; FfxResourceDesc description; uint32_t state; };
struct FfxDispatchUpscale {
    FfxHeader header; void *commandList;
    FfxResource color, depth, motionVectors, exposure, reactive, transparency, output;
    FfxFloat2 jitterOffset, motionVectorScale;
    FfxDimensions2D renderSize, upscaleSize;
    bool enableSharpening; uint8_t pad0[3]; float sharpness, frameTimeDelta, preExposure;
    bool reset; uint8_t pad1[3];
    float cameraNear, cameraFar, cameraFovAngleVertical, viewSpaceToMetersFactor;
    uint32_t flags;
};
static_assert(sizeof(FfxResource) == 48 && offsetof(FfxDispatchUpscale, color) == 24 && offsetof(FfxDispatchUpscale, output) == 312);
using FfxDispatchFn = uint32_t (*)(void **, const FfxHeader *);
FfxDispatchFn g_ffx_dispatch = nullptr;

void log(const char *format, ...) {
    AcquireSRWLockExclusive(&g_log_lock);
    if (FILE *file = _wfopen(kLog, L"ab")) {
        va_list args;
        va_start(args, format);
        vfprintf(file, format, args);
        va_end(args);
        fclose(file);
    }
    ReleaseSRWLockExclusive(&g_log_lock);
}

bool on_main_device(void *resource) {
    if (!resource || !g_device) return false;
    ID3D12Device *device = nullptr;
    const HRESULT hr = static_cast<ID3D12Resource *>(resource)->GetDevice(IID_PPV_ARGS(&device));
    const bool same = SUCCEEDED(hr) && device == g_device;
    if (device) device->Release();
    return same;
}

bool record_frame_bridge(ID3D12GraphicsCommandList *commands, ID3D12Resource *color,
                         uint32_t render_width, uint32_t render_height,
                         unsigned long long frame);
bool record_block0(ID3D12GraphicsCommandList *commands, unsigned long long frame);
bool record_blocks1_4(ID3D12GraphicsCommandList *commands, unsigned long long frame);
bool record_blocks5_8(ID3D12GraphicsCommandList *commands, unsigned long long frame);
bool record_blocks9_14(ID3D12GraphicsCommandList *commands, unsigned long long frame);
bool record_blocks15_22(ID3D12GraphicsCommandList *commands, unsigned long long frame);

uint32_t hook_ffx_dispatch(void **context, const FfxHeader *header) {
    const auto n = ++g_ffx_frames;
    if (header && (header->type & 0x00ffffffu) == 0x00010001u) {
        const auto *dispatch = reinterpret_cast<const FfxDispatchUpscale *>(header);
        const bool resources_same_device = on_main_device(dispatch->color.resource) &&
            on_main_device(dispatch->depth.resource) && on_main_device(dispatch->motionVectors.resource) &&
            on_main_device(dispatch->output.resource);
        const bool target = dispatch->upscaleSize.width == 1920 && dispatch->upscaleSize.height == 1080;
        g_frame_contract_ready.store(resources_same_device && target && dispatch->commandList != nullptr);
        if (resources_same_device && dispatch->commandList && g_frame_bridge_ready.load()) {
            auto *commands = static_cast<ID3D12GraphicsCommandList *>(dispatch->commandList);
            if (record_frame_bridge(commands, static_cast<ID3D12Resource *>(dispatch->color.resource),
                                    dispatch->renderSize.width, dispatch->renderSize.height, n) &&
                record_block0(commands, n))
                if(record_blocks1_4(commands, n)&&record_blocks5_8(commands, n)&&record_blocks9_14(commands,n))record_blocks15_22(commands,n);
        }
        if (n == 1 || n % 120 == 0) {
            log("ffx_frame=%llu cmd=%p render=%ux%u output=%ux%u target1080=%u same_device=%u jitter=%.7g,%.7g color=%p[%u,%ux%u,s%u] depth=%p[%u,%ux%u,s%u] motion=%p[%u,%ux%u,s%u] output_resource=%p[%u,%ux%u,s%u]\n",
                n, dispatch->commandList, dispatch->renderSize.width, dispatch->renderSize.height,
                dispatch->upscaleSize.width, dispatch->upscaleSize.height, target ? 1u : 0u,
                resources_same_device ? 1u : 0u, dispatch->jitterOffset.x, dispatch->jitterOffset.y,
                dispatch->color.resource, dispatch->color.description.format, dispatch->color.description.width,
                dispatch->color.description.height, dispatch->color.state, dispatch->depth.resource,
                dispatch->depth.description.format, dispatch->depth.description.width,
                dispatch->depth.description.height, dispatch->depth.state, dispatch->motionVectors.resource,
                dispatch->motionVectors.description.format, dispatch->motionVectors.description.width,
                dispatch->motionVectors.description.height, dispatch->motionVectors.state,
                dispatch->output.resource, dispatch->output.description.format,
                dispatch->output.description.width, dispatch->output.description.height, dispatch->output.state);
        }
    }
    return g_ffx_dispatch(context, header);
}

DWORD WINAPI ffx_hook_worker(void *) {
    for (unsigned i = 0; i < 600; ++i) {
        if (HMODULE module = GetModuleHandleW(L"amd_fidelityfx_dx12.dll")) {
            void *target = reinterpret_cast<void *>(GetProcAddress(module, "ffxDispatch"));
            const MH_STATUS init = MH_Initialize();
            const MH_STATUS create = target ? MH_CreateHook(target, reinterpret_cast<void *>(&hook_ffx_dispatch),
                reinterpret_cast<void **>(&g_ffx_dispatch)) : MH_ERROR_NOT_EXECUTABLE;
            const MH_STATUS enable = create == MH_OK ? MH_EnableHook(target) : create;
            g_ffx_hook_ready.store((init == MH_OK || init == MH_ERROR_ALREADY_INITIALIZED) && create == MH_OK && enable == MH_OK);
            log("ffx_hook init=%d create=%d enable=%d module=%p target=%p ready=%u\n",
                static_cast<int>(init), static_cast<int>(create), static_cast<int>(enable), module, target,
                g_ffx_hook_ready.load() ? 1u : 0u);
            return g_ffx_hook_ready.load() ? 0 : 1;
        }
        Sleep(100);
    }
    log("ffx_hook timeout\n");
    return 1;
}

ID3D12Resource *make_buffer(UINT64 bytes, D3D12_HEAP_TYPE heap_type,
                            D3D12_RESOURCE_STATES state,
                            D3D12_RESOURCE_FLAGS flags) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heap_type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    ID3D12Resource *resource = nullptr;
    dmlrt_check("probe CreateCommittedResource",
        g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                          state, nullptr,
                                          IID_PPV_ARGS(&resource)));
    return resource;
}

void initialize_frame_bridge() {
    constexpr UINT64 tile_bytes = 8160ull * 256 * sizeof(float);
    g_frame_tiles = make_buffer(tile_bytes, D3D12_HEAP_TYPE_DEFAULT,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const char shader[] = R"(
cbuffer Params : register(b0) { uint render_width; uint render_height; };
Texture2D<float4> color : register(t0);
RWStructuredBuffer<float> tiles : register(u0);
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= 960 || id.y >= 544) return;
    float2 source = (float2(id.xy) + .5) * float2(render_width, render_height) / float2(960, 544) - .5;
    int2 p0 = int2(floor(source));
    float2 f = source - floor(source);
    p0 = clamp(p0, int2(0,0), int2(int(render_width)-1, int(render_height)-1));
    int2 p1 = min(p0 + 1, int2(int(render_width)-1, int(render_height)-1));
    float4 a = lerp(color.Load(int3(p0.x, p0.y, 0)), color.Load(int3(p1.x, p0.y, 0)), f.x);
    float4 b = lerp(color.Load(int3(p0.x, p1.y, 0)), color.Load(int3(p1.x, p1.y, 0)), f.x);
    float4 value = saturate(lerp(a, b, f.y));
    uint tile = (id.y / 8) * 120 + id.x / 8;
    uint local = ((id.y % 8) * 8 + id.x % 8) * 4;
    [unroll] for (uint c = 0; c < 4; ++c) tiles[tile * 256 + local + c] = value[c];
})";
    ID3DBlob *code = nullptr, *error = nullptr;
    dmlrt_check("frame bridge compile", D3DCompile(shader, sizeof(shader) - 1, nullptr,
        nullptr, nullptr, "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &code, &error));
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1};
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants = {0, 0, 2};
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable = {2, ranges};
    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.NumParameters = 2;
    root_desc.pParameters = parameters;
    ID3DBlob *signature = nullptr;
    dmlrt_check("frame bridge signature", D3D12SerializeRootSignature(
        &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    dmlrt_check("frame bridge root", g_device->CreateRootSignature(0,
        signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&g_frame_bridge_root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
    pipeline_desc.pRootSignature = g_frame_bridge_root;
    pipeline_desc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    dmlrt_check("frame bridge pso", g_device->CreateComputePipelineState(
        &pipeline_desc, IID_PPV_ARGS(&g_frame_bridge_pso)));
    const UINT step = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (auto &heap : g_frame_bridge_heaps) {
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        dmlrt_check("frame bridge heap", g_device->CreateDescriptorHeap(&heap_desc,
                                                                        IID_PPV_ARGS(&heap)));
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += step;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.StructureByteStride = sizeof(float);
        uav.Buffer.NumElements = static_cast<UINT>(tile_bytes / sizeof(float));
        g_device->CreateUnorderedAccessView(g_frame_tiles, nullptr, &uav, cpu);
    }
    g_frame_bridge_ready.store(true);
    log("frame_bridge_ready target=960x544 tiles=8160 bytes=%llu heaps=8\n",
        static_cast<unsigned long long>(tile_bytes));
}

bool record_frame_bridge(ID3D12GraphicsCommandList *commands, ID3D12Resource *color,
                         uint32_t render_width, uint32_t render_height,
                         unsigned long long frame) {
    if (!commands || !color || !g_frame_bridge_ready.load() || !render_width || !render_height)
        return false;
    ID3D12DescriptorHeap *heap = g_frame_bridge_heaps[frame & 7];
    if (g_frame_bridge_submissions.load()) {
        D3D12_RESOURCE_BARRIER transition{};
        transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transition.Transition.pResource = g_frame_tiles;
        transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        transition.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        transition.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        commands->ResourceBarrier(1, &transition);
    }
    auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
    const auto desc = color->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    g_device->CreateShaderResourceView(color, &srv, cpu);
    ID3D12DescriptorHeap *heaps[] = {heap};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetComputeRootSignature(g_frame_bridge_root);
    const UINT constants[2] = {render_width, render_height};
    commands->SetComputeRoot32BitConstants(0, 2, constants, 0);
    commands->SetComputeRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
    commands->SetPipelineState(g_frame_bridge_pso);
    commands->Dispatch(120, 68, 1);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = g_frame_tiles;
    commands->ResourceBarrier(1, &barrier);
    D3D12_RESOURCE_BARRIER transition{};
    transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition.pResource = g_frame_tiles;
    transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    commands->ResourceBarrier(1, &transition);
    const auto submitted = ++g_frame_bridge_submissions;
    if (submitted == 1 || submitted % 120 == 0)
        log("frame_bridge_submit=%llu ffx_frame=%llu render=%ux%u color=%p format=%u\n",
            submitted, frame, render_width, render_height, color, static_cast<unsigned>(desc.Format));
    return true;
}

std::vector<unsigned char> read_runtime_file(const wchar_t *name) {
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, LR"(D:\DLSSNR-Lab\%ls)", name);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw DmlFailure{"block0 weight file", HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)};
    const size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<unsigned char> data(size);
    file.read(reinterpret_cast<char *>(data.data()), size);
    return data;
}

ID3D12Resource *upload_runtime_resource(const std::vector<unsigned char> &data,
                                        std::vector<ID3D12Resource *> &uploads) {
    ID3D12Resource *dst=make_buffer(data.size(),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),*src=make_buffer(data.size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE);void *mapped=nullptr;D3D12_RANGE none{0,0};dmlrt_check("runtime upload map",src->Map(0,&none,&mapped));std::memcpy(mapped,data.data(),data.size());src->Unmap(0,nullptr);g_list->CopyBufferRegion(dst,0,src,0,data.size());D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=dst;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;g_list->ResourceBarrier(1,&b);uploads.push_back(src);return dst;
}

void initialize_block0() {
    constexpr UINT tiles = 8160;
    constexpr UINT64 p192 = UINT64(tiles) * 192 * 2;
    constexpr UINT64 h256 = UINT64(tiles) * 256 * 2;
    constexpr UINT64 o2048 = UINT64(tiles) * 2048 * 2;
    constexpr UINT64 out_bytes = UINT64(tiles) * 2048 * 4;
    g_block0_l1 = new DmlGemmOperator();
    g_block0_l2 = new DmlGemmOperator();
    g_block0_l3 = new DmlGemmOperator();
    g_block0_l1->Create(g_dml, g_device, 1, tiles, 192, 256);
    g_block0_l2->Create(g_dml, g_device, 1, tiles, 256, 256);
    g_block0_l3->Create(g_dml, g_device, 1, tiles, 256, 2048);

    dmlrt_check("block0 allocator reset", g_allocator->Reset());
    dmlrt_check("block0 list reset", g_list->Reset(g_allocator, nullptr));
    g_block0_l1->RecordInitialization(g_recorder, g_list);
    g_block0_l2->RecordInitialization(g_recorder, g_list);
    g_block0_l3->RecordInitialization(g_recorder, g_list);

    auto w1 = read_runtime_file(L"block0-directml-layer1_weight.f16");
    auto b1 = read_runtime_file(L"block0-directml-layer1_bias.f32");
    auto w2 = read_runtime_file(L"block0-directml-layer2_weight.f16");
    auto b2 = read_runtime_file(L"block0-directml-layer2_bias.f32");
    auto w3 = read_runtime_file(L"block0-directml-layer3_weight.f16");
    auto b3 = read_runtime_file(L"block0-directml-layer3_bias.f32");
    auto output_bias = read_runtime_file(L"block0-directml-output_bias.f32");
    auto output_scale = read_runtime_file(L"block0-directml-output_scale.f32");
    auto tile_map = read_runtime_file(L"block0-directml-tile-map.u16");
    if (w1.size()!=192*256*2||b1.size()!=256*4||w2.size()!=256*256*2||b2.size()!=256*4||
        w3.size()!=256*2048*2||b3.size()!=2048*4||output_bias.size()!=2048*4||
        output_scale.size()!=2048*4||tile_map.size()!=4*512*2)
        throw DmlFailure{"block0 weight size", E_INVALIDARG};

    g_block0_packed = make_buffer(p192, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_block0_raw1 = make_buffer(h256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_block0_hidden1 = make_buffer(h256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_block0_raw2 = make_buffer(h256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_block0_hidden2 = make_buffer(h256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_block0_raw3 = make_buffer(o2048, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_block0_tile = make_buffer(out_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_block0_hwc = make_buffer(out_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    struct Constant { ID3D12Resource *resource; std::vector<unsigned char> *data; };
    std::vector<ID3D12Resource *> uploads;
    auto upload = [&](const std::vector<unsigned char> &data) {
        ID3D12Resource *dst = make_buffer(data.size(), D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ID3D12Resource *src = make_buffer(data.size(), D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
        void *mapped = nullptr; D3D12_RANGE none{0,0};
        dmlrt_check("block0 upload map", src->Map(0, &none, &mapped));
        std::memcpy(mapped, data.data(), data.size()); src->Unmap(0, nullptr);
        g_list->CopyBufferRegion(dst, 0, src, 0, data.size());
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=dst;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        g_list->ResourceBarrier(1,&barrier);uploads.push_back(src);return dst;
    };
    ID3D12Resource *rw1=upload(w1),*rb1=upload(b1),*rw2=upload(w2),*rb2=upload(b2),
                     *rw3=upload(w3),*rb3=upload(b3),*rbo=upload(output_bias),
                     *rscale=upload(output_scale),*rmap=upload(tile_map);

    const char shader[] = R"(
StructuredBuffer<float> src:register(t0),b1:register(t1),b2:register(t2),b3:register(t3),bo:register(t4),scale:register(t5);
ByteAddressBuffer raw1:register(t6),raw2:register(t7),raw3:register(t8);StructuredBuffer<float> tile:register(t9);ByteAddressBuffer map:register(t10);
RWByteAddressBuffer packed:register(u0),h1:register(u1),h2:register(u2);RWStructuredBuffer<float> output:register(u3),hwc:register(u4);
float H(ByteAddressBuffer b,uint i){uint x=b.Load((i&~1)*2);return f16tof32((x>>((i&1)*16))&65535);}float silu(float x){return x/(1+exp(-x));}
float F(float x){if(x==0)return 0;float q=x<0?-1:1,a=abs(x);if(a<.015625)return q*min(round(a*512),7)/512;float e=clamp(floor(log2(a)),-6.,8.),m=round((a/exp2(e)-1)*8);if(m>=8){m=0;e+=1;}return q*min(exp2(e)*(1+m/8),448.);}
[numthreads(64,1,1)]void pack_input(uint3 id:SV_DispatchThreadID){uint i=(id.x+id.y*4194240)*2;if(i>=8160*192)return;float v[2];[unroll]for(uint z=0;z<2;z++){uint n=i+z,t=n/192,j=n%192;v[z]=src[t*256+(j/3)*4+j%3];}packed.Store(i*2,f32tof16(v[0])|(f32tof16(v[1])<<16));}
[numthreads(64,1,1)]void post1(uint3 id:SV_DispatchThreadID){uint i=(id.x+id.y*4194240)*2;if(i>=8160*256)return;h1.Store(i*2,f32tof16(silu(H(raw1,i)+b1[i%256]))|(f32tof16(silu(H(raw1,i+1)+b1[(i+1)%256]))<<16));}
[numthreads(64,1,1)]void post2(uint3 id:SV_DispatchThreadID){uint i=(id.x+id.y*4194240)*2;if(i>=8160*256)return;h2.Store(i*2,f32tof16(silu(H(raw2,i)+b2[i%256]))|(f32tof16(silu(H(raw2,i+1)+b2[(i+1)%256]))<<16));}
[numthreads(64,1,1)]void post3(uint3 id:SV_DispatchThreadID){uint i=id.x+id.y*4194240;if(i<8160*2048){uint j=i%2048;output[i]=(H(raw3,i)+b3[j])*scale[j]+bo[j];}}
[numthreads(64,1,1)]void reorder(uint3 id:SV_DispatchThreadID){uint z=id.x+id.y*4194240;if(z>=8160*2048)return;uint c=z%32,p=z/32,x=p%960,y=p/960,tileId=(y/8)*120+x/8,q=((y%8)/4)*2+(x%8)/4,local=((y%4)*4+x%4)*32+c,mi=q*512+local,word=map.Load((mi&~1)*2),rank=(word>>((mi&1)*16))&65535;hwc[z]=F(tile[tileId*2048+q*512+rank]);})";
    const char *entries[5]={"pack_input","post1","post2","post3","reorder"};
    ID3DBlob *error=nullptr;
    for(UINT i=0;i<5;i++){ID3DBlob *code=nullptr;dmlrt_check("block0 compile",D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,entries[i],"cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error));if(!g_block0_root){D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,11,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,5,0,0,11}};D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.DescriptorTable={2,ranges};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=1;desc.pParameters=&parameter;ID3DBlob *signature=nullptr;dmlrt_check("block0 signature",D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error));dmlrt_check("block0 root",g_device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&g_block0_root)));}D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=g_block0_root;desc.CS={code->GetBufferPointer(),code->GetBufferSize()};dmlrt_check("block0 pso",g_device->CreateComputePipelineState(&desc,IID_PPV_ARGS(&g_block0_pso[i])));}
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,16,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    dmlrt_check("block0 heap",g_device->CreateDescriptorHeap(&heap_desc,IID_PPV_ARGS(&g_block0_heap)));
    UINT step=g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto cpu=g_block0_heap->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Buffer.StructureByteStride=4;for(auto pair:{std::pair<ID3D12Resource*,UINT>{g_frame_tiles,8160*256},{rb1,256},{rb2,256},{rb3,2048},{rbo,2048},{rscale,2048}}){sv.Buffer.NumElements=pair.second;g_device->CreateShaderResourceView(pair.first,&sv,cpu);cpu.ptr+=step;}sv.Format=DXGI_FORMAT_R32_TYPELESS;sv.Buffer.StructureByteStride=0;sv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;for(auto pair:{std::pair<ID3D12Resource*,UINT64>{g_block0_raw1,h256},{g_block0_raw2,h256},{g_block0_raw3,o2048}}){sv.Buffer.NumElements=(UINT)(pair.second/4);g_device->CreateShaderResourceView(pair.first,&sv,cpu);cpu.ptr+=step;}sv.Format=DXGI_FORMAT_UNKNOWN;sv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_NONE;sv.Buffer.StructureByteStride=4;sv.Buffer.NumElements=(UINT)(out_bytes/4);g_device->CreateShaderResourceView(g_block0_tile,&sv,cpu);cpu.ptr+=step;sv.Format=DXGI_FORMAT_R32_TYPELESS;sv.Buffer.StructureByteStride=0;sv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;sv.Buffer.NumElements=(UINT)(tile_map.size()/4);g_device->CreateShaderResourceView(rmap,&sv,cpu);cpu.ptr+=step;D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Format=DXGI_FORMAT_R32_TYPELESS;uv.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;for(auto pair:{std::pair<ID3D12Resource*,UINT64>{g_block0_packed,p192},{g_block0_hidden1,h256},{g_block0_hidden2,h256}}){uv.Buffer.NumElements=(UINT)(pair.second/4);g_device->CreateUnorderedAccessView(pair.first,nullptr,&uv,cpu);cpu.ptr+=step;}uv.Format=DXGI_FORMAT_UNKNOWN;uv.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_NONE;uv.Buffer.StructureByteStride=4;uv.Buffer.NumElements=(UINT)(out_bytes/4);g_device->CreateUnorderedAccessView(g_block0_tile,nullptr,&uv,cpu);cpu.ptr+=step;g_device->CreateUnorderedAccessView(g_block0_hwc,nullptr,&uv,cpu);

    dmlrt_check("block0 init close",g_list->Close());ID3D12CommandList *lists[]={g_list};g_queue->ExecuteCommandLists(1,lists);dmlrt_check("block0 init signal",g_queue->Signal(g_fence,3));dmlrt_check("block0 init event",g_fence->SetEventOnCompletion(3,g_event));if(WaitForSingleObject(g_event,30000)!=WAIT_OBJECT_0)throw DmlFailure{"block0 init wait",HRESULT_FROM_WIN32(ERROR_TIMEOUT)};for(auto *resource:uploads)resource->Release();
    g_block0_l1->Bind(g_block0_packed,rw1,g_block0_raw1);g_block0_l2->Bind(g_block0_hidden1,rw2,g_block0_raw2);g_block0_l3->Bind(g_block0_hidden2,rw3,g_block0_raw3);g_block0_ready.store(true);log("block0_ready tiles=8160 hwc=960x544 output=%p\n",g_block0_hwc);
}

void initialize_blocks1_4() {
    constexpr UINT tokens=960*544;
    constexpr UINT64 tensor_bytes=UINT64(tokens)*32*4,qkv_bytes=UINT64(tokens)*48*4;
    g_front_main[0]=make_buffer(tensor_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_front_main[1]=make_buffer(tensor_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_front_feature=make_buffer(tensor_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_front_qkv=make_buffer(qkv_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const wchar_t *weight_names[4]={L"block1-effective.bin",L"block2-effective.bin",L"block3-effective.bin",L"block4-body-effective.bin"};
    for(UINT i=0;i<4;i++){auto data=read_runtime_file(weight_names[i]);if(data.size()!=41220)throw DmlFailure{"front weight size",E_INVALIDARG};g_front_weights[i]=make_buffer(data.size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE);void *mapped=nullptr;D3D12_RANGE none{0,0};dmlrt_check("front weight map",g_front_weights[i]->Map(0,&none,&mapped));std::memcpy(mapped,data.data(),data.size());g_front_weights[i]->Unmap(0,nullptr);}
    D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,4}};
    D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.DescriptorTable={2,ranges};D3D12_ROOT_SIGNATURE_DESC root_desc{};root_desc.NumParameters=1;root_desc.pParameters=&parameter;ID3DBlob *signature=nullptr,*error=nullptr;dmlrt_check("front signature",D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error));dmlrt_check("front root",g_device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&g_front_root)));
    const UINT shifts[4]={0,1,1,1},versions[3]={1,2,2};const wchar_t *passes[3]={L"ffn",L"qkv",L"attention"};
    for(UINT b=0;b<4;b++)for(UINT p=0;p<3;p++){wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,LR"(D:\DLSSNR-Lab\shader-cache\block1-v%u-960x544-s%u-%ls.cso)",versions[p],shifts[b],passes[p]);ID3DBlob *code=nullptr;dmlrt_check("front shader",D3DReadFileToBlob(path,&code));D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=g_front_root;desc.CS={code->GetBufferPointer(),code->GetBufferSize()};dmlrt_check("front pso",g_device->CreateComputePipelineState(&desc,IID_PPV_ARGS(&g_front_pso[b][p])));}
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,28,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};dmlrt_check("front heap",g_device->CreateDescriptorHeap(&heap_desc,IID_PPV_ARGS(&g_front_heap)));UINT step=g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto cpu=g_front_heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT b=0;b<4;b++){ID3D12Resource *source=b?g_front_main[(b-1)&1]:g_block0_hwc,*dest=g_front_main[b&1];D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Format=DXGI_FORMAT_R32_TYPELESS;srv.Buffer.NumElements=41220/4;srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;g_device->CreateShaderResourceView(g_front_weights[b],&srv,cpu);cpu.ptr+=step;srv.Format=DXGI_FORMAT_UNKNOWN;srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_NONE;srv.Buffer.StructureByteStride=4;for(auto pair:{std::pair<ID3D12Resource*,UINT64>{source,tensor_bytes},{g_front_feature,tensor_bytes},{g_front_qkv,qkv_bytes}}){srv.Buffer.NumElements=(UINT)(pair.second/4);g_device->CreateShaderResourceView(pair.first,&srv,cpu);cpu.ptr+=step;}D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Buffer.StructureByteStride=4;for(auto pair:{std::pair<ID3D12Resource*,UINT64>{g_front_feature,tensor_bytes},{dest,tensor_bytes},{g_front_qkv,qkv_bytes}}){uav.Buffer.NumElements=(UINT)(pair.second/4);g_device->CreateUnorderedAccessView(pair.first,nullptr,&uav,cpu);cpu.ptr+=step;}}
    g_front_ready.store(true);log("front_blocks1_4_ready tokens=%u block4=%p bytes=%llu\n",tokens,g_front_main[1],(unsigned long long)tensor_bytes);
}

void initialize_blocks5_8() {
    constexpr UINT L=4,T=130560,C=64,H=96,Q=96,A=32;
    dmlrt_check("s64 allocator reset",g_allocator->Reset());dmlrt_check("s64 list reset",g_list->Reset(g_allocator,nullptr));
    for(UINT i=0;i<L;i++){g_s64_gate[i].Create(g_dml,g_device,1,T,C,H);g_s64_up[i].Create(g_dml,g_device,1,T,C,H);g_s64_project[i].Create(g_dml,g_device,1,T,H,C);g_s64_qkv_op[i].Create(g_dml,g_device,1,T,C,Q);g_s64_attention_project[i].Create(g_dml,g_device,1,T,A,C);for(auto *op:{&g_s64_gate[i],&g_s64_up[i],&g_s64_project[i],&g_s64_qkv_op[i],&g_s64_attention_project[i]})op->RecordInitialization(g_recorder,g_list);}
    const UINT64 main_bytes=UINT64(T)*C*2,hidden_bytes=UINT64(T)*H*2,qkv_bytes=UINT64(T)*Q*2,attention_bytes=UINT64(T)*A*2;
    g_s64_main[0]=make_buffer(main_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);g_s64_main[1]=make_buffer(main_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);g_s64_gate_out=make_buffer(hidden_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);g_s64_up_out=make_buffer(hidden_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);g_s64_hidden=make_buffer(hidden_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);g_s64_project_raw=make_buffer(main_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);g_s64_feature=make_buffer(main_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);g_s64_qkv_raw=make_buffer(qkv_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);g_s64_qkv_float=make_buffer(qkv_bytes*2,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);g_s64_attention_float=make_buffer(attention_bytes*2,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);g_s64_attention=make_buffer(attention_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);g_s64_attention_raw=make_buffer(main_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);g_s64_mid=make_buffer(544ull*960*32*4,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    std::vector<ID3D12Resource*> uploads;
    auto upload=[&](const std::vector<unsigned char>&data){ID3D12Resource *dst=make_buffer(data.size(),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),*src=make_buffer(data.size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE);void *mapped=nullptr;D3D12_RANGE none{0,0};dmlrt_check("s64 upload map",src->Map(0,&none,&mapped));std::memcpy(mapped,data.data(),data.size());src->Unmap(0,nullptr);g_list->CopyBufferRegion(dst,0,src,0,data.size());D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=dst;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;g_list->ResourceBarrier(1,&b);uploads.push_back(src);return dst;};
    g_s64_down=upload(read_runtime_file(L"block4-downsample-matrix.bin"));g_s64_enter=upload(read_runtime_file(L"block5-enter-32x64.bin"));
    const wchar_t *parts[9]={L"expand.f16",L"up.f16",L"project.f16",L"qkv.f16",L"attention_project.f16",L"ffn_skip.f32",L"attention_skip.f32",L"bias.f32",L"scale.f32"};
    for(UINT i=0;i<L;i++){ID3D12Resource **dest[9]={&g_s64_gw[i],&g_s64_uw[i],&g_s64_pw[i],&g_s64_qw[i],&g_s64_aw[i],&g_s64_fs[i],&g_s64_as[i],&g_s64_bias[i],&g_s64_scale[i]};for(UINT j=0;j<9;j++){wchar_t name[128];swprintf(name,128,L"block%u-body-effective-%ls",5+i,parts[j]);*dest[j]=upload(read_runtime_file(name));}}
    D3D12_RESOURCE_BARRIER constants[2]{};ID3D12Resource *constant_resources[2]={g_s64_down,g_s64_enter};for(UINT i=0;i<2;i++){constants[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;constants[i].Transition.pResource=constant_resources[i];constants[i].Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;constants[i].Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;constants[i].Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}g_list->ResourceBarrier(2,constants);
    g_s64_predown.Create(g_device,g_s64_down,g_s64_enter,g_front_main[1],g_s64_mid,g_s64_main[0]);const UINT shifts[L]={0,1,3,2};for(UINT i=0;i<L;i++){UINT p=i&1,n=p^1;g_s64_gate[i].Bind(g_s64_main[p],g_s64_gw[i],g_s64_gate_out);g_s64_up[i].Bind(g_s64_main[p],g_s64_uw[i],g_s64_up_out);g_s64_project[i].Bind(g_s64_hidden,g_s64_pw[i],g_s64_project_raw);g_s64_qkv_op[i].Bind(g_s64_feature,g_s64_qw[i],g_s64_qkv_raw);g_s64_attention_project[i].Bind(g_s64_attention,g_s64_aw[i],g_s64_attention_raw);g_s64_boundary[i].Create(g_device,g_s64_gate_out,g_s64_up_out,g_s64_project_raw,g_s64_main[p],g_s64_fs[i],g_s64_attention_raw,g_s64_feature,g_s64_as[i],g_s64_hidden,g_s64_feature,g_s64_main[n]);g_s64_window[i].Create(g_device,g_s64_qkv_raw,g_s64_qkv_float,g_s64_bias[i],g_s64_scale[i],g_s64_attention_float,g_s64_attention,shifts[i]);}
    dmlrt_check("s64 init close",g_list->Close());ID3D12CommandList *lists[]={g_list};g_queue->ExecuteCommandLists(1,lists);dmlrt_check("s64 init signal",g_queue->Signal(g_fence,4));dmlrt_check("s64 init event",g_fence->SetEventOnCompletion(4,g_event));if(WaitForSingleObject(g_event,30000)!=WAIT_OBJECT_0)throw DmlFailure{"s64 init wait",HRESULT_FROM_WIN32(ERROR_TIMEOUT)};for(auto *resource:uploads)resource->Release();g_s64_ready.store(true);log("blocks5_8_ready block4=%p block8=%p tokens=%u\n",g_front_main[1],g_s64_main[0],T);
}

template<class R, UINT L>
void initialize_swin_stage(R &s, UINT T, UINT C, UINT H, UINT Q, UINT A,
                           UINT first_block, ID3D12Resource *source,
                           UINT source_t,
                           const wchar_t *down_name, const wchar_t *enter_name,
                           const UINT (&shifts)[L], UINT64 fence_value,
                           std::atomic<bool> &ready, const char *label) {
    dmlrt_check("stage allocator reset",g_allocator->Reset());
    dmlrt_check("stage list reset",g_list->Reset(g_allocator,nullptr));
    for(UINT i=0;i<L;i++){
        s.gate[i].Create(g_dml,g_device,1,T,C,H);s.up[i].Create(g_dml,g_device,1,T,C,H);
        s.project[i].Create(g_dml,g_device,1,T,H,C);s.qkv[i].Create(g_dml,g_device,1,T,C,Q);
        s.attention_project[i].Create(g_dml,g_device,1,T,A,C);
        for(auto *op:{&s.gate[i],&s.up[i],&s.project[i],&s.qkv[i],&s.attention_project[i]})op->RecordInitialization(g_recorder,g_list);
    }
    const UINT64 main_bytes=UINT64(T)*C*2,hidden_bytes=UINT64(T)*H*2,qkv_bytes=UINT64(T)*Q*2,attention_bytes=UINT64(T)*A*2;
    auto gpu=[&](UINT64 n){return make_buffer(n,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);};
    s.main[0]=gpu(main_bytes);s.main[1]=gpu(main_bytes);s.gate_out=gpu(hidden_bytes);s.up_out=gpu(hidden_bytes);s.hidden=gpu(hidden_bytes);
    s.project_raw=gpu(main_bytes);s.feature=gpu(main_bytes);s.qkv_raw=gpu(qkv_bytes);s.qkv_float=gpu(qkv_bytes*2);
    s.attention_float=gpu(attention_bytes*2);s.attention=gpu(attention_bytes);s.attention_raw=gpu(main_bytes);
    const UINT source_c=C/2;s.input_fp32=gpu(UINT64(source_t)*source_c*4);s.mid=gpu(UINT64(source_t)*source_c*4);
    std::vector<ID3D12Resource*> uploads;
    s.down=upload_runtime_resource(read_runtime_file(down_name),uploads);s.enter=upload_runtime_resource(read_runtime_file(enter_name),uploads);
    const wchar_t *parts[9]={L"expand.f16",L"up.f16",L"project.f16",L"qkv.f16",L"attention_project.f16",L"ffn_skip.f32",L"attention_skip.f32",L"bias.f32",L"scale.f32"};
    for(UINT i=0;i<L;i++){
        ID3D12Resource **dest[9]={&s.gw[i],&s.uw[i],&s.pw[i],&s.qw[i],&s.aw[i],&s.fs[i],&s.as[i],&s.bias[i],&s.scale[i]};
        for(UINT j=0;j<9;j++){wchar_t name[128];const wchar_t *flavor=(first_block==9&&i>=1&&i<=4)?L"effective":L"body-effective";swprintf(name,128,L"block%u-%ls-%ls",first_block+i,flavor,parts[j]);*dest[j]=upload_runtime_resource(read_runtime_file(name),uploads);}
    }
    D3D12_RESOURCE_BARRIER constants[2]{};ID3D12Resource *cr[2]={s.down,s.enter};
    for(UINT i=0;i<2;i++){constants[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;constants[i].Transition.pResource=cr[i];constants[i].Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;constants[i].Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;constants[i].Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}g_list->ResourceBarrier(2,constants);
    s.bridge.Create(g_device,source,s.input_fp32,source_t*source_c);s.predown.Create(g_device,s.down,s.enter,s.input_fp32,s.mid,s.main[0]);
    for(UINT i=0;i<L;i++){UINT p=i&1,n=p^1;s.gate[i].Bind(s.main[p],s.gw[i],s.gate_out);s.up[i].Bind(s.main[p],s.uw[i],s.up_out);s.project[i].Bind(s.hidden,s.pw[i],s.project_raw);s.qkv[i].Bind(s.feature,s.qw[i],s.qkv_raw);s.attention_project[i].Bind(s.attention,s.aw[i],s.attention_raw);s.boundary[i].Create(g_device,s.gate_out,s.up_out,s.project_raw,s.main[p],s.fs[i],s.attention_raw,s.feature,s.as[i],s.hidden,s.feature,s.main[n]);s.window[i].Create(g_device,s.qkv_raw,s.qkv_float,s.bias[i],s.scale[i],s.attention_float,s.attention,shifts[i]);}
    dmlrt_check("stage init close",g_list->Close());ID3D12CommandList *lists[]={g_list};g_queue->ExecuteCommandLists(1,lists);dmlrt_check("stage init signal",g_queue->Signal(g_fence,fence_value));dmlrt_check("stage init event",g_fence->SetEventOnCompletion(fence_value,g_event));if(WaitForSingleObject(g_event,30000)!=WAIT_OBJECT_0)throw DmlFailure{"stage init wait",HRESULT_FROM_WIN32(ERROR_TIMEOUT)};for(auto *r:uploads)r->Release();ready.store(true);log("%s_ready input=%p output=%p tokens=%u\n",label,source,s.main[0],T);
}

void initialize_blocks9_14(){const UINT shifts[6]={0,1,3,2,0,1};initialize_swin_stage<RuntimeS128,6>(g_s128,32640,128,160,192,64,9,g_s64_main[0],130560,L"block8-downsample-matrix.bin",L"block9-enter-64x128.bin",shifts,5,g_s128_ready,"blocks9_14");}
void initialize_blocks15_22(){const UINT shifts[8]={0,1,3,2,0,1,3,2};initialize_swin_stage<RuntimeS256,8>(g_s256,8640,256,288,384,128,15,g_s128.main[0],32640,L"block14-downsample-matrix.bin",L"block15-enter-128x256.bin",shifts,6,g_s256_ready,"blocks15_22");}

bool record_block0(ID3D12GraphicsCommandList *commands, unsigned long long frame) {
    if(!commands||!g_block0_ready.load())return false;
    if(g_front_submissions.load()){
        D3D12_RESOURCE_BARRIER transition{};transition.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;transition.Transition.pResource=g_block0_hwc;transition.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;transition.Transition.StateBefore=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;transition.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;commands->ResourceBarrier(1,&transition);
    }
    auto custom=[&](UINT pass,UINT64 threads){ID3D12DescriptorHeap *heaps[]={g_block0_heap};commands->SetDescriptorHeaps(1,heaps);commands->SetComputeRootSignature(g_block0_root);commands->SetComputeRootDescriptorTable(0,g_block0_heap->GetGPUDescriptorHandleForHeapStart());commands->SetPipelineState(g_block0_pso[pass]);UINT64 groups=(threads+63)/64;commands->Dispatch((UINT)std::min<UINT64>(groups,65535),(UINT)((groups+65534)/65535),1);};
    auto barrier=[&](ID3D12Resource *resource){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=resource;commands->ResourceBarrier(1,&b);};
    custom(0,8160*192/2);barrier(g_block0_packed);g_block0_l1->Record(g_recorder,commands);barrier(g_block0_raw1);custom(1,8160*256/2);barrier(g_block0_hidden1);g_block0_l2->Record(g_recorder,commands);barrier(g_block0_raw2);custom(2,8160*256/2);barrier(g_block0_hidden2);g_block0_l3->Record(g_recorder,commands);barrier(g_block0_raw3);custom(3,8160*2048);barrier(g_block0_tile);custom(4,8160*2048);barrier(g_block0_hwc);
    const auto submitted=++g_block0_submissions;if(submitted==1||submitted%120==0)log("block0_submit=%llu ffx_frame=%llu hwc=%p\n",submitted,frame,g_block0_hwc);return true;
}

bool record_blocks1_4(ID3D12GraphicsCommandList *commands, unsigned long long frame) {
    if(!commands||!g_front_ready.load())return false;
    auto transition=[&](ID3D12Resource *resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=resource;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=before;b.Transition.StateAfter=after;commands->ResourceBarrier(1,&b);};
    if(g_front_submissions.load()){transition(g_front_feature,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_front_qkv,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_front_main[0],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_front_main[1],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    transition(g_block0_hwc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap *heaps[]={g_front_heap};commands->SetDescriptorHeaps(1,heaps);commands->SetComputeRootSignature(g_front_root);const UINT step=g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);constexpr UINT groups=(960*544+63)/64;
    for(UINT b=0;b<4;b++){if(b){transition(g_front_feature,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_front_qkv,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);if(b>=2)transition(g_front_main[b&1],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}auto gpu=g_front_heap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(b)*7*step;commands->SetComputeRootDescriptorTable(0,gpu);commands->SetPipelineState(g_front_pso[b][0]);commands->Dispatch(groups,1,1);transition(g_front_feature,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);commands->SetPipelineState(g_front_pso[b][1]);commands->Dispatch(groups,1,1);transition(g_front_qkv,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);commands->SetPipelineState(g_front_pso[b][2]);commands->Dispatch(groups,1,1);transition(g_front_main[b&1],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
    const auto submitted=++g_front_submissions;if(submitted==1||submitted%120==0)log("front_blocks1_4_submit=%llu ffx_frame=%llu block4=%p\n",submitted,frame,g_front_main[1]);return true;
}

bool record_blocks5_8(ID3D12GraphicsCommandList *commands, unsigned long long frame) {
    if(!commands||!g_s64_ready.load())return false;
    auto uav=[&](ID3D12Resource *resource){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=resource;commands->ResourceBarrier(1,&b);};
    if(g_s128_submissions.load()){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=g_s64_main[0];b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;commands->ResourceBarrier(1,&b);}
    if(g_s64_submissions.load()){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=g_s64_mid;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;commands->ResourceBarrier(1,&b);}
    g_s64_predown.Record(commands,0);D3D12_RESOURCE_BARRIER mid{};mid.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;mid.Transition.pResource=g_s64_mid;mid.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;mid.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;mid.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;commands->ResourceBarrier(1,&mid);g_s64_predown.Record(commands,1);uav(g_s64_main[0]);
    for(UINT i=0;i<4;i++){g_s64_gate[i].Record(g_recorder,commands);uav(g_s64_gate_out);g_s64_up[i].Record(g_recorder,commands);uav(g_s64_up_out);g_s64_boundary[i].Record(commands,0);uav(g_s64_hidden);g_s64_project[i].Record(g_recorder,commands);uav(g_s64_project_raw);g_s64_boundary[i].Record(commands,1);uav(g_s64_feature);g_s64_qkv_op[i].Record(g_recorder,commands);uav(g_s64_qkv_raw);g_s64_window[i].Record(commands,0);uav(g_s64_qkv_float);g_s64_window[i].Record(commands,1);uav(g_s64_attention_float);g_s64_window[i].Record(commands,2);uav(g_s64_attention);g_s64_attention_project[i].Record(g_recorder,commands);uav(g_s64_attention_raw);g_s64_boundary[i].Record(commands,2);uav(g_s64_main[(i&1)^1]);}
    const auto submitted=++g_s64_submissions;if(submitted==1||submitted%120==0)log("blocks5_8_submit=%llu ffx_frame=%llu block8=%p\n",submitted,frame,g_s64_main[0]);return true;
}

template<class R, UINT L>
bool record_swin_stage(R &s, ID3D12Resource *source, ID3D12GraphicsCommandList *commands,
                       unsigned long long frame, std::atomic<bool> &ready,
                       std::atomic<unsigned long long> &submissions, const char *label) {
    if(!commands||!ready.load())return false;
    auto uav=[&](ID3D12Resource *r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;commands->ResourceBarrier(1,&b);};
    auto transition=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES bstate){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=a;b.Transition.StateAfter=bstate;commands->ResourceBarrier(1,&b);};
    if(submissions.load()){transition(s.main[0],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(s.input_fp32,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(s.mid,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    transition(source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);s.bridge.Record(commands);transition(s.input_fp32,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    s.predown.Record(commands,0);transition(s.mid,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);s.predown.Record(commands,1);uav(s.main[0]);
    for(UINT i=0;i<L;i++){s.gate[i].Record(g_recorder,commands);uav(s.gate_out);s.up[i].Record(g_recorder,commands);uav(s.up_out);s.boundary[i].Record(commands,0);uav(s.hidden);s.project[i].Record(g_recorder,commands);uav(s.project_raw);s.boundary[i].Record(commands,1);uav(s.feature);s.qkv[i].Record(g_recorder,commands);uav(s.qkv_raw);s.window[i].Record(commands,0);uav(s.qkv_float);s.window[i].Record(commands,1);uav(s.attention_float);s.window[i].Record(commands,2);uav(s.attention);s.attention_project[i].Record(g_recorder,commands);uav(s.attention_raw);s.boundary[i].Record(commands,2);uav(s.main[(i&1)^1]);}
    const auto n=++submissions;if(n==1||n%120==0)log("%s_submit=%llu ffx_frame=%llu output=%p\n",label,n,frame,s.main[0]);return true;
}

bool record_blocks9_14(ID3D12GraphicsCommandList *commands,unsigned long long frame){return record_swin_stage<RuntimeS128,6>(g_s128,g_s64_main[0],commands,frame,g_s128_ready,g_s128_submissions,"blocks9_14");}
bool record_blocks15_22(ID3D12GraphicsCommandList *commands,unsigned long long frame){return record_swin_stage<RuntimeS256,8>(g_s256,g_s128.main[0],commands,frame,g_s256_ready,g_s256_submissions,"blocks15_22");}

void run_warm_probe() {
    constexpr UINT iterations = 100;
    const UINT64 input_bytes = g_operator->ABytes();
    const UINT64 weight_bytes = g_operator->BBytes();
    const UINT64 output_bytes = g_operator->OutputBytes();
    g_probe_input = make_buffer(input_bytes, D3D12_HEAP_TYPE_DEFAULT,
                                D3D12_RESOURCE_STATE_COPY_DEST,
                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_probe_weight = make_buffer(weight_bytes, D3D12_HEAP_TYPE_DEFAULT,
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_probe_output = make_buffer(output_bytes, D3D12_HEAP_TYPE_DEFAULT,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Resource *zero = make_buffer(input_bytes, D3D12_HEAP_TYPE_UPLOAD,
                                       D3D12_RESOURCE_STATE_GENERIC_READ,
                                       D3D12_RESOURCE_FLAG_NONE);
    void *mapped = nullptr;
    D3D12_RANGE none{0, 0};
    dmlrt_check("probe Map", zero->Map(0, &none, &mapped));
    std::memset(mapped, 0, static_cast<size_t>(input_bytes));
    zero->Unmap(0, nullptr);

    dmlrt_check("probe allocator reset", g_allocator->Reset());
    dmlrt_check("probe list reset", g_list->Reset(g_allocator, nullptr));
    g_list->CopyBufferRegion(g_probe_input, 0, zero, 0, input_bytes);
    g_list->CopyBufferRegion(g_probe_weight, 0, zero, 0, weight_bytes);
    D3D12_RESOURCE_BARRIER transitions[2]{};
    for (UINT i = 0; i < 2; ++i) {
        transitions[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transitions[i].Transition.pResource = i ? g_probe_weight : g_probe_input;
        transitions[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        transitions[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        transitions[i].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    g_list->ResourceBarrier(2, transitions);
    g_operator->Bind(g_probe_input, g_probe_weight, g_probe_output);

    D3D12_QUERY_HEAP_DESC query_desc{};
    query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query_desc.Count = 2;
    ID3D12QueryHeap *queries = nullptr;
    dmlrt_check("probe query heap",
                g_device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&queries)));
    ID3D12Resource *readback = make_buffer(16, D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    g_list->EndQuery(queries, D3D12_QUERY_TYPE_TIMESTAMP, 0);
    for (UINT i = 0; i < iterations; ++i) {
        g_operator->Record(g_recorder, g_list);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = g_probe_output;
        g_list->ResourceBarrier(1, &barrier);
    }
    g_list->EndQuery(queries, D3D12_QUERY_TYPE_TIMESTAMP, 1);
    g_list->ResolveQueryData(queries, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                             readback, 0);
    dmlrt_check("probe close", g_list->Close());
    ID3D12CommandList *lists[] = {g_list};
    g_queue->ExecuteCommandLists(1, lists);
    dmlrt_check("probe signal", g_queue->Signal(g_fence, 2));
    dmlrt_check("probe completion", g_fence->SetEventOnCompletion(2, g_event));
    if (WaitForSingleObject(g_event, 30000) != WAIT_OBJECT_0)
        throw DmlFailure{"probe wait", HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    UINT64 *timestamps = nullptr;
    D3D12_RANGE range{0, 16};
    dmlrt_check("probe timestamp map", readback->Map(0, &range,
                                                     reinterpret_cast<void **>(&timestamps)));
    UINT64 frequency = 0;
    dmlrt_check("probe timestamp frequency", g_queue->GetTimestampFrequency(&frequency));
    g_probe_gpu_ms = 1000.0 * double(timestamps[1] - timestamps[0]) /
                     double(frequency) / iterations;
    readback->Unmap(0, nullptr);
    queries->Release();
    readback->Release();
    zero->Release();
}

DWORD WINAPI initialize_worker(void *) {
    const ULONGLONG begin = GetTickCount64();
    HMODULE library = LoadLibraryW(L"DirectML.dll");
    auto create = library ? reinterpret_cast<CreateDml>(
                                GetProcAddress(library, "DMLCreateDevice"))
                          : nullptr;
    HRESULT hr = create ? create(g_device, DML_CREATE_DEVICE_FLAG_NONE,
                                 kDmlDevice, reinterpret_cast<void **>(&g_dml))
                        : HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(hr))
        hr = g_dml->CreateCommandRecorder(kDmlRecorder,
                                          reinterpret_cast<void **>(&g_recorder));
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    if (SUCCEEDED(hr)) hr = g_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&g_queue));
    if (SUCCEEDED(hr)) hr = g_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_allocator));
    if (SUCCEEDED(hr)) hr = g_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_allocator, nullptr,
        IID_PPV_ARGS(&g_list));
    if (SUCCEEDED(hr)) hr = g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                   IID_PPV_ARGS(&g_fence));
    if (SUCCEEDED(hr)) g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (SUCCEEDED(hr) && !g_event) hr = HRESULT_FROM_WIN32(GetLastError());

    if (SUCCEEDED(hr)) {
        g_operator = new DmlGemmOperator();
        try {
            g_operator->Create(g_dml, g_device, 1, 8160, 192, 256);
            g_operator->RecordInitialization(g_recorder, g_list);
        } catch (const DmlFailure &failure) {
            hr = failure.result;
            log("resident_operator_failed operation=%s hr=0x%08x\n",
                failure.operation, static_cast<unsigned>(failure.result));
        }
    }
    if (SUCCEEDED(hr)) hr = g_list->Close();
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[] = {g_list};
        g_queue->ExecuteCommandLists(1, lists);
        hr = g_queue->Signal(g_fence, 1);
    }
    if (SUCCEEDED(hr)) hr = g_fence->SetEventOnCompletion(1, g_event);
    if (SUCCEEDED(hr) && WaitForSingleObject(g_event, 30000) != WAIT_OBJECT_0)
        hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    if (SUCCEEDED(hr)) {
        try {
            run_warm_probe();
            initialize_frame_bridge();
            initialize_block0();
            initialize_blocks1_4();
            initialize_blocks5_8();
            initialize_blocks9_14();
            initialize_blocks15_22();
        } catch (const DmlFailure &failure) {
            hr = failure.result;
            log("resident_execution_failed operation=%s hr=0x%08x\n",
                failure.operation, static_cast<unsigned>(failure.result));
        }
    }
    const HRESULT removed = g_device->GetDeviceRemovedReason();
    if (SUCCEEDED(hr) && SUCCEEDED(removed)) {
        g_ready.store(true);
        log("resident_ready device=%p dml=%p operator=8160x192x256 gpu_ms=%.6f iterations=100 init_wall_ms=%llu removed=0x%08x\n",
            g_device, g_dml, g_probe_gpu_ms, GetTickCount64() - begin,
            static_cast<unsigned>(removed));
    } else {
        g_failed.store(true);
        log("resident_failed hr=0x%08x removed=0x%08x wall_ms=%llu\n",
            static_cast<unsigned>(hr), static_cast<unsigned>(removed),
            GetTickCount64() - begin);
    }
    return SUCCEEDED(hr) && SUCCEEDED(removed) ? 0 : 1;
}

void on_init_swapchain(reshade::api::swapchain *swapchain, bool resize) {
    if (!resize || g_started.load()) return;
    auto *native = reinterpret_cast<IDXGISwapChain3 *>(
        static_cast<uintptr_t>(swapchain->get_native()));
    ID3D12Device *device = nullptr;
    const HRESULT hr = native ? native->GetDevice(IID_PPV_ARGS(&device)) : E_POINTER;
    if (FAILED(hr)) {
        log("resident_swapchain_device_failed swapchain=%p hr=0x%08x\n",
            native, static_cast<unsigned>(hr));
        return;
    }
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) {
        device->Release();
        return;
    }
    g_device = device;
    DXGI_SWAP_CHAIN_DESC desc{};
    native->GetDesc(&desc);
    log("resident_start swapchain=%p device=%p size=%ux%u format=%u\n",
        native, g_device, desc.BufferDesc.Width, desc.BufferDesc.Height,
        static_cast<unsigned>(desc.BufferDesc.Format));
    if (HANDLE thread = CreateThread(nullptr, 0, initialize_worker, nullptr, 0, nullptr))
        CloseHandle(thread);
    else {
        g_failed.store(true);
        log("resident_thread_failed error=%lu\n", GetLastError());
    }
}

void on_present(reshade::api::command_queue *, reshade::api::swapchain *,
                const reshade::api::rect *, const reshade::api::rect *, uint32_t,
                const reshade::api::rect *) {
    const auto n = ++g_presents;
    if (n == 1 || n % 600 == 0)
        log("present=%llu backend_ready=%u failed=%u ffx_hook=%u ffx_frames=%llu frame_contract_1080=%u\n",
            n, g_ready.load() ? 1 : 0, g_failed.load() ? 1 : 0,
            g_ffx_hook_ready.load() ? 1 : 0, g_ffx_frames.load(),
            g_frame_contract_ready.load() ? 1 : 0);
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&initialize_worker), &pinned))
            return FALSE;
        DeleteFileW(kLog);
        if (!reshade::register_addon(instance)) return FALSE;
        reshade::register_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
        reshade::register_event<reshade::addon_event::present>(on_present);
        if (HANDLE thread = CreateThread(nullptr, 0, ffx_hook_worker, nullptr, 0, nullptr))
            CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        reshade::unregister_event<reshade::addon_event::present>(on_present);
        reshade::unregister_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
        reshade::unregister_addon(instance);
    }
    return TRUE;
}
