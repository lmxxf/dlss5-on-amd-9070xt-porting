#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <type_traits>
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
#include "runtime_gpu_profile.h"
#include "swin64_1080_runtime.h"
#include "swin128_1080_runtime.h"
#include "swin256_1080_runtime.h"
#include "swin512_1080_runtime.h"
#include "fp16_bridge_runtime.h"
#include "fp16_crop_runtime.h"
#include "vit_1080_runtime.h"
#include "block39_1080_runtime.h"
#include "block48_1080_runtime.h"
#include "block56_1080_runtime.h"
#include "block62_1080_runtime.h"
#include "block66_1080_runtime.h"

extern "C" __declspec(dllexport) const char *NAME = "DLSS5 AMD 1080p Runtime";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Initializes a persistent DirectML operator on the game's D3D12 device.";

namespace {
constexpr wchar_t kLog[] = LR"(D:\DLSSNR-Lab\logs\dlss5-1080p-runtime.txt)";
static const GUID kDmlDevice = {0x6dbd6437, 0x96fd, 0x423f,
    {0xa9, 0x8c, 0xae, 0x5e, 0x7c, 0x2a, 0x57, 0x3f}};
static const GUID kDmlRecorder = {0xe6857a76, 0x2e3e, 0x4fdd,
    {0xbf, 0xf4, 0x5d, 0x2b, 0xa1, 0x0f, 0xb4, 0x53}};
static const GUID kUnwrappedObject={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
using CreateDml = HRESULT(WINAPI *)(ID3D12Device *, DML_CREATE_DEVICE_FLAGS,
                                    REFIID, void **);

SRWLOCK g_log_lock = SRWLOCK_INIT;
std::atomic<bool> g_started{false}, g_ready{false}, g_failed{false};
UINT g_max_block=70;bool g_present_enabled=false;
RuntimeGpuProfile g_gpu_profile;
UINT g_output_mode=0;bool g_output_key_down=false;
std::atomic<unsigned long long> g_presents{0};
ID3D12Device *g_device = nullptr;
ID3D12Device *g_bridge_device = nullptr;
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
ID3D12Resource *g_frame_tiles_bridge = nullptr;
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
struct RuntimeS512 {
    DmlGemmOperator gate[8],up[8],project[8],qkv[8],attention_project[8];
    ID3D12Resource *main[2]{},*gate_out{},*up_out{},*hidden{},*project_raw{},*feature{},*qkv_raw{},*qkv_float{},*attention_float{},*attention{},*attention_raw{},*mid{},*input_fp32{},*down{},*enter{};
    ID3D12Resource *gw[8]{},*uw[8]{},*pw[8]{},*qw[8]{},*aw[8]{},*fs[8]{},*as[8]{},*bias[8]{},*scale[8]{};
    Fp16ToFp32RuntimePass bridge;Swin512Predown1080Pass predown;Swin512Boundary1080Pass boundary[8];Swin512Window1080Pass window[8];
} g_s512;
std::atomic<bool> g_s128_ready{false},g_s256_ready{false};
std::atomic<bool> g_s512_ready{false};
std::atomic<unsigned long long> g_s128_submissions{0},g_s256_submissions{0},g_s512_submissions{0};
struct RuntimeVit {
    DmlGemmOperator expand[8],contract[8],qkv[8],qk[8],av[8],projection[8];
    ID3D12Resource *source[2]{},*cropped{},*predown_mid{},*main{},*branch{},*contract_input{},*contract_raw{},*hidden{},*qkv_out{},*q{},*k{},*v{},*score{},*prob{},*attention{},*attention_input{},*projection_raw{};
    ID3D12Resource *down{},*enter{},*contract_weight{},*contract_skip{},*projection_weight{},*projection_skip{},*expand_weight[8]{},*qkv_weight[8]{},*scale[8]{};
    Fp16Crop512RuntimePass crop;VitPredown1080Pass predown;VitFront1080Pass front[2];VitQkvPack1080Pass pack[8];VitSoftmax1080Pass softmax;VitOutput1080Pass output[2];
} g_vit;
std::atomic<bool> g_vit_ready{false};std::atomic<unsigned long long> g_vit_submissions{0};
struct RuntimeD512 {
    DmlGemmOperator prefix,gate[8],up[8],project[8],qkv[8],attention_project[8];
    ID3D12Resource *main[2]{},*combined{},*prefix_raw{},*prefix_weight{},*prefix_bias{},*gate_out{},*up_out{},*hidden{},*project_raw{},*feature{},*qkv_raw{},*qkv_float{},*attention_float{},*attention{},*attention_raw{};
    ID3D12Resource *gw[8]{},*uw[8]{},*pw[8]{},*qw[8]{},*aw[8]{},*fs[8]{},*as[8]{},*bias[8]{},*scale[8]{};
    Block39_1080Pass prefix_pass;Swin512Boundary1080Pass boundary[8];Swin512Window1080Pass window[8];
} g_d512;
std::atomic<bool> g_d512_ready{false};std::atomic<unsigned long long> g_d512_submissions{0};
struct RuntimeD256 {
    DmlGemmOperator prefix,gate[8],up[8],project[8],qkv[8],attention_project[8];
    ID3D12Resource *main[2]{},*packed{},*prefix_raw{},*prefix_weight{},*prefix_bias{},*gate_out{},*up_out{},*hidden{},*project_raw{},*feature{},*qkv_raw{},*qkv_float{},*attention_float{},*attention{},*attention_raw{};
    ID3D12Resource *gw[8]{},*uw[8]{},*pw[8]{},*qw[8]{},*aw[8]{},*fs[8]{},*as[8]{},*bias[8]{},*scale[8]{};
    Block48_1080Pass prefix_pass;Swin256Boundary1080Pass boundary[8];Swin256Window1080Pass window[8];
} g_d256;
std::atomic<bool> g_d256_ready{false};std::atomic<unsigned long long> g_d256_submissions{0};
struct RuntimeD128 {
    DmlGemmOperator prefix,gate[6],up[6],project[6],qkv[6],attention_project[6];
    ID3D12Resource *main[2]{},*packed{},*prefix_raw{},*prefix_weight{},*prefix_bias{},*gate_out{},*up_out{},*hidden{},*project_raw{},*feature{},*qkv_raw{},*qkv_float{},*attention_float{},*attention{},*attention_raw{};
    ID3D12Resource *gw[6]{},*uw[6]{},*pw[6]{},*qw[6]{},*aw[6]{},*fs[6]{},*as[6]{},*bias[6]{},*scale[6]{};
    Block56_1080Pass prefix_pass;Swin128Boundary1080Pass boundary[6];Swin128Window1080Pass window[6];
} g_d128;
std::atomic<bool> g_d128_ready{false};std::atomic<unsigned long long> g_d128_submissions{0};
struct RuntimeD64 {
    DmlGemmOperator prefix,gate[4],up[4],project[4],qkv[4],attention_project[4];ID3D12Resource *main[2]{},*packed{},*prefix_raw{},*prefix_weight{},*prefix_bias{},*gate_out{},*up_out{},*hidden{},*project_raw{},*feature{},*qkv_raw{},*qkv_float{},*attention_float{},*attention{},*attention_raw{};ID3D12Resource *gw[4]{},*uw[4]{},*pw[4]{},*qw[4]{},*aw[4]{},*fs[4]{},*as[4]{},*bias[4]{},*scale[4]{};Block62_1080Pass prefix_pass;Swin64Boundary1080Pass boundary[4];Swin64Window1080Pass window[4];
} g_d64;std::atomic<bool> g_d64_ready{false};std::atomic<unsigned long long> g_d64_submissions{0};
struct RuntimeD32 {DmlGemmOperator prefix;ID3D12Resource *packed{},*raw{},*half{},*prefix_fp32{},*weight{},*bias{},*main[2]{},*feature{},*qkv{},*body_weights[4]{};Block66_1080Pass prefix_pass;Fp16ToFp32RuntimePass bridge;ID3D12RootSignature*root{};ID3D12PipelineState*pso[4][3]{};ID3D12DescriptorHeap*heap{};} g_d32;
std::atomic<bool> g_d32_ready{false};std::atomic<unsigned long long> g_d32_submissions{0};
struct RuntimeBlock70 {ID3D12Resource *sparse{},*body_weight{},*out_weight{},*prefix{},*feature{},*qkv{},*body{},*packed{};ID3D12RootSignature *prefix_root{},*body_root{},*output_root{};ID3D12PipelineState *prefix_pso{},*body_pso[3]{},*out_pso{},*pack_pso{};ID3D12DescriptorHeap *prefix_heap{},*body_heap{},*output_heap[8]{};UINT nnz{};} g_b70;
std::atomic<bool> g_b70_ready{false};std::atomic<unsigned long long> g_b70_submissions{0};
ID3D12Resource *g_display_texture=nullptr;
unsigned long long g_display_generation=0;
ULONGLONG g_cadence_start=0;unsigned long long g_cadence_frames=0;
std::atomic<bool> g_b70_packed_copied{false};IDXGISwapChain3 *g_main_swapchain=nullptr;

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
    const LUID a=SUCCEEDED(hr)?device->GetAdapterLuid():LUID{},b=g_device?g_device->GetAdapterLuid():LUID{};
    const bool same=SUCCEEDED(hr)&&a.HighPart==b.HighPart&&a.LowPart==b.LowPart;
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
bool record_blocks23_30(ID3D12GraphicsCommandList *commands, unsigned long long frame);
bool record_blocks31_38(ID3D12GraphicsCommandList *commands, unsigned long long frame);
bool record_blocks39_47(ID3D12GraphicsCommandList *commands, unsigned long long frame);
bool record_blocks48_55(ID3D12GraphicsCommandList *commands, unsigned long long frame);
bool record_blocks56_61(ID3D12GraphicsCommandList *commands, unsigned long long frame);
bool record_blocks62_65(ID3D12GraphicsCommandList *commands, unsigned long long frame);
bool record_blocks66_69(ID3D12GraphicsCommandList *commands, unsigned long long frame);
bool record_block70(ID3D12GraphicsCommandList *commands,ID3D12Resource *output,unsigned long long frame);
DWORD WINAPI initialize_worker(void *);

uint32_t hook_ffx_dispatch(void **context, const FfxHeader *header) {
    const auto n = ++g_ffx_frames;
    const bool is_upscale = header && (header->type & 0x00ffffffu) == 0x00010001u;
    FfxDispatchUpscale snapshot{};
    ID3D12Resource *held_resources[4]{};ID3D12GraphicsCommandList*held_commands=nullptr,*native_commands=nullptr;
    if (is_upscale) {snapshot = *reinterpret_cast<const FfxDispatchUpscale *>(header);held_resources[0]=static_cast<ID3D12Resource*>(snapshot.color.resource);held_resources[1]=static_cast<ID3D12Resource*>(snapshot.depth.resource);held_resources[2]=static_cast<ID3D12Resource*>(snapshot.motionVectors.resource);held_resources[3]=static_cast<ID3D12Resource*>(snapshot.output.resource);for(auto*r:held_resources)if(r)r->AddRef();held_commands=static_cast<ID3D12GraphicsCommandList*>(snapshot.commandList);if(held_commands){held_commands->AddRef();held_commands->QueryInterface(kUnwrappedObject,reinterpret_cast<void**>(&native_commands));}if(held_commands&&native_commands&&g_device&&!g_started.load()){ID3D12Device*device=nullptr;if(SUCCEEDED(held_commands->GetDevice(IID_PPV_ARGS(&device)))){bool expected=false;if(g_started.compare_exchange_strong(expected,true)){g_bridge_device=device;log("resident_start_ffx proxy_list=%p native_list=%p native_device=%p bridge_device=%p\n",held_commands,native_commands,g_device,g_bridge_device);if(HANDLE thread=CreateThread(nullptr,0,initialize_worker,nullptr,0,nullptr))CloseHandle(thread);else{g_failed.store(true);log("resident_thread_failed error=%lu\n",GetLastError());}}else device->Release();}}}
    bool color_same=false,depth_same=false,motion_same=false,output_same=false,command_same=false,resources_same_device=false,target=false,bridge_ok=false;
    if (is_upscale) {
        const auto *dispatch = &snapshot;
        color_same=on_main_device(dispatch->color.resource);depth_same=on_main_device(dispatch->depth.resource);motion_same=on_main_device(dispatch->motionVectors.resource);output_same=on_main_device(dispatch->output.resource);
        ID3D12Device*command_device=nullptr;const bool command_get=dispatch->commandList&&SUCCEEDED(static_cast<ID3D12GraphicsCommandList*>(dispatch->commandList)->GetDevice(IID_PPV_ARGS(&command_device)));const LUID ca=command_get?command_device->GetAdapterLuid():LUID{},cb=g_device?g_device->GetAdapterLuid():LUID{};command_same=command_get&&ca.HighPart==cb.HighPart&&ca.LowPart==cb.LowPart;if(command_device)command_device->Release();
        resources_same_device=color_same&&depth_same&&motion_same&&output_same&&command_same;
        target=dispatch->output.description.width==1920&&dispatch->output.description.height==1080;
        g_frame_contract_ready.store(resources_same_device && target && dispatch->commandList != nullptr);
        if(resources_same_device&&target&&dispatch->commandList&&g_ready.load())bridge_ok=record_frame_bridge(static_cast<ID3D12GraphicsCommandList*>(dispatch->commandList),static_cast<ID3D12Resource*>(dispatch->color.resource),dispatch->renderSize.width,dispatch->renderSize.height,n);
    }
    const uint32_t result=g_ffx_dispatch(context,header);
    if(is_upscale){const auto*dispatch=&snapshot;if(bridge_ok&&native_commands&&g_max_block!=999){auto*commands=native_commands;const bool profiling=g_max_block==70&&g_gpu_profile.Begin(commands,g_b70_submissions.load());bool ok=record_block0(commands,n);if(profiling)g_gpu_profile.Mark(commands,1);if(ok&&g_max_block>=4){ok=record_blocks1_4(commands,n);if(profiling)g_gpu_profile.Mark(commands,2);}if(ok&&g_max_block>=8){ok=record_blocks5_8(commands,n);if(profiling)g_gpu_profile.Mark(commands,3);}if(ok&&g_max_block>=14){ok=record_blocks9_14(commands,n);if(profiling)g_gpu_profile.Mark(commands,4);}if(ok&&g_max_block>=22){ok=record_blocks15_22(commands,n);if(profiling)g_gpu_profile.Mark(commands,5);}if(ok&&g_max_block>=30){ok=record_blocks23_30(commands,n);if(profiling)g_gpu_profile.Mark(commands,6);}if(ok&&g_max_block>=38){ok=record_blocks31_38(commands,n);if(profiling)g_gpu_profile.Mark(commands,7);}if(ok&&g_max_block>=47){ok=record_blocks39_47(commands,n);if(profiling)g_gpu_profile.Mark(commands,8);}if(ok&&g_max_block>=55){ok=record_blocks48_55(commands,n);if(profiling)g_gpu_profile.Mark(commands,9);}if(ok&&g_max_block>=61){ok=record_blocks56_61(commands,n);if(profiling)g_gpu_profile.Mark(commands,10);}if(ok&&g_max_block>=65){ok=record_blocks62_65(commands,n);if(profiling)g_gpu_profile.Mark(commands,11);}if(ok&&g_max_block>=69){ok=record_blocks66_69(commands,n);if(profiling)g_gpu_profile.Mark(commands,12);}if(ok&&g_max_block>=70)record_block70(commands,static_cast<ID3D12Resource*>(dispatch->output.resource),n);if(profiling){g_gpu_profile.Mark(commands,13);g_gpu_profile.End(commands);}}
        if (n == 1 || n % 120 == 0) {
            log("ffx_frame=%llu cmd=%p render=%ux%u abi_output=%ux%u target1080=%u same_device=%u device_parts=%u%u%u%u%u jitter=%.7g,%.7g color=%p[%u,%ux%u,s%u] depth=%p[%u,%ux%u,s%u] motion=%p[%u,%ux%u,s%u] output_resource=%p[%u,%ux%u,s%u]\n",
                n, dispatch->commandList, dispatch->renderSize.width, dispatch->renderSize.height,
                dispatch->upscaleSize.width, dispatch->upscaleSize.height, target ? 1u : 0u,
                resources_same_device ? 1u : 0u,color_same,depth_same,motion_same,output_same,command_same,dispatch->jitterOffset.x, dispatch->jitterOffset.y,
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
    if(is_upscale){for(auto*r:held_resources)if(r)r->Release();if(native_commands)native_commands->Release();if(held_commands)held_commands->Release();}
    return result;
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
    if(!g_bridge_device)throw DmlFailure{"bridge device",E_POINTER};D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=tile_bytes;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;bd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;dmlrt_check("frame bridge shared resource",g_device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_SHARED,&bd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&g_frame_tiles)));HANDLE shared=nullptr;dmlrt_check("frame bridge shared handle",g_device->CreateSharedHandle(g_frame_tiles,nullptr,GENERIC_ALL,nullptr,&shared));dmlrt_check("frame bridge open shared",g_bridge_device->OpenSharedHandle(shared,IID_PPV_ARGS(&g_frame_tiles_bridge)));CloseHandle(shared);
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
    dmlrt_check("frame bridge root", g_bridge_device->CreateRootSignature(0,
        signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&g_frame_bridge_root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
    pipeline_desc.pRootSignature = g_frame_bridge_root;
    pipeline_desc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    dmlrt_check("frame bridge pso", g_bridge_device->CreateComputePipelineState(
        &pipeline_desc, IID_PPV_ARGS(&g_frame_bridge_pso)));
    const UINT step = g_bridge_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (auto &heap : g_frame_bridge_heaps) {
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        dmlrt_check("frame bridge heap", g_bridge_device->CreateDescriptorHeap(&heap_desc,
                                                                        IID_PPV_ARGS(&heap)));
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += step;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.StructureByteStride = sizeof(float);
        uav.Buffer.NumElements = static_cast<UINT>(tile_bytes / sizeof(float));
        g_bridge_device->CreateUnorderedAccessView(g_frame_tiles_bridge, nullptr, &uav, cpu);
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
        transition.Transition.pResource = g_frame_tiles_bridge;
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
    g_bridge_device->CreateShaderResourceView(color, &srv, cpu);
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
    barrier.UAV.pResource = g_frame_tiles_bridge;
    commands->ResourceBarrier(1, &barrier);
    D3D12_RESOURCE_BARRIER transition{};
    transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition.pResource = g_frame_tiles_bridge;
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
                           std::atomic<bool> &ready, const char *label,
                           bool logical512=false) {
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
    const wchar_t *body_parts[9]={L"expand.f16",L"up.f16",L"project.f16",L"qkv.f16",L"attention_project.f16",L"ffn_skip.f32",L"attention_skip.f32",L"bias.f32",L"scale.f32"};
    const wchar_t *logical_parts[9]={L"gate.f16",L"up.f16",L"project.f16",L"qkv.f16",L"attention_project.f16",L"ffn_skip.f32",L"attention_skip.f32",L"attention_bias.f32",L"attention_scale.f32"};
    for(UINT i=0;i<L;i++){
        ID3D12Resource **dest[9]={&s.gw[i],&s.uw[i],&s.pw[i],&s.qw[i],&s.aw[i],&s.fs[i],&s.as[i],&s.bias[i],&s.scale[i]};
        for(UINT j=0;j<9;j++){wchar_t name[128];const wchar_t *flavor=logical512?L"logical-effective":((first_block==9&&i>=1&&i<=4)?L"effective":L"body-effective");const wchar_t *part=logical512?logical_parts[j]:body_parts[j];swprintf(name,128,L"block%u-%ls-%ls",first_block+i,flavor,part);*dest[j]=upload_runtime_resource(read_runtime_file(name),uploads);}
    }
    D3D12_RESOURCE_BARRIER constants[2]{};ID3D12Resource *cr[2]={s.down,s.enter};
    for(UINT i=0;i<2;i++){constants[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;constants[i].Transition.pResource=cr[i];constants[i].Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;constants[i].Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;constants[i].Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}g_list->ResourceBarrier(2,constants);
    s.bridge.Create(g_device,source,s.input_fp32,source_t*source_c);s.predown.Create(g_device,s.down,s.enter,s.input_fp32,s.mid,s.main[0]);
    for(UINT i=0;i<L;i++){UINT p=i&1,n=p^1;s.gate[i].Bind(s.main[p],s.gw[i],s.gate_out);s.up[i].Bind(s.main[p],s.uw[i],s.up_out);s.project[i].Bind(s.hidden,s.pw[i],s.project_raw);s.qkv[i].Bind(s.feature,s.qw[i],s.qkv_raw);s.attention_project[i].Bind(s.attention,s.aw[i],s.attention_raw);s.boundary[i].Create(g_device,s.gate_out,s.up_out,s.project_raw,s.main[p],s.fs[i],s.attention_raw,s.feature,s.as[i],s.hidden,s.feature,s.main[n]);s.window[i].Create(g_device,s.qkv_raw,s.qkv_float,s.bias[i],s.scale[i],s.attention_float,s.attention,shifts[i]);}
    dmlrt_check("stage init close",g_list->Close());ID3D12CommandList *lists[]={g_list};g_queue->ExecuteCommandLists(1,lists);dmlrt_check("stage init signal",g_queue->Signal(g_fence,fence_value));dmlrt_check("stage init event",g_fence->SetEventOnCompletion(fence_value,g_event));if(WaitForSingleObject(g_event,30000)!=WAIT_OBJECT_0)throw DmlFailure{"stage init wait",HRESULT_FROM_WIN32(ERROR_TIMEOUT)};for(auto *r:uploads)r->Release();ready.store(true);log("%s_ready input=%p output=%p tokens=%u\n",label,source,s.main[0],T);
}

void initialize_blocks9_14(){const UINT shifts[6]={0,1,3,2,0,1};initialize_swin_stage<RuntimeS128,6>(g_s128,32640,128,160,192,64,9,g_s64_main[0],130560,L"block8-downsample-matrix.bin",L"block9-enter-64x128.bin",shifts,5,g_s128_ready,"blocks9_14");}
void initialize_blocks15_22(){const UINT shifts[8]={0,1,3,2,0,1,3,2};initialize_swin_stage<RuntimeS256,8>(g_s256,8640,256,288,384,128,15,g_s128.main[0],32640,L"block14-downsample-matrix.bin",L"block15-enter-128x256.bin",shifts,6,g_s256_ready,"blocks15_22");}
void initialize_blocks23_30(){const UINT shifts[8]={0,1,3,2,0,1,3,2};initialize_swin_stage<RuntimeS512,8>(g_s512,2560,512,256,768,256,23,g_s256.main[0],8160,L"block22-pool-identity.bin",L"block22-enter-256x512.bin",shifts,7,g_s512_ready,"blocks23_30",true);}

void initialize_blocks31_38(){
    constexpr UINT L=8,T=540,H=32,D=32;constexpr UINT64 main_bytes=UINT64(T)*1024*2,branch_bytes=UINT64(T)*4096*2,qkv_bytes=UINT64(T)*3072*2,qv_bytes=UINT64(H)*T*D*2,score_bytes=UINT64(H)*T*T*2;
    dmlrt_check("vit allocator reset",g_allocator->Reset());dmlrt_check("vit list reset",g_list->Reset(g_allocator,nullptr));
    for(UINT i=0;i<L;i++){g_vit.expand[i].Create(g_dml,g_device,1,T,1024,4096);g_vit.contract[i].Create(g_dml,g_device,1,T,4096,1024);g_vit.qkv[i].Create(g_dml,g_device,1,T,1024,3072);g_vit.qk[i].Create(g_dml,g_device,H,T,D,T,DML_MATRIX_TRANSFORM_TRANSPOSE);g_vit.av[i].Create(g_dml,g_device,H,T,T,D);g_vit.projection[i].Create(g_dml,g_device,1,T,1024,1024);for(auto *op:{&g_vit.expand[i],&g_vit.contract[i],&g_vit.qkv[i],&g_vit.qk[i],&g_vit.av[i],&g_vit.projection[i]})op->RecordInitialization(g_recorder,g_list);}
    auto gpu=[&](UINT64 n){return make_buffer(n,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);};
    g_vit.source[0]=gpu(main_bytes*2);g_vit.source[1]=gpu(main_bytes*2);g_vit.cropped=gpu(34ull*60*512*4);g_vit.predown_mid=gpu(34ull*60*512*4);g_vit.main=gpu(main_bytes);g_vit.branch=gpu(branch_bytes);g_vit.contract_input=gpu(branch_bytes);g_vit.contract_raw=gpu(main_bytes);g_vit.hidden=gpu(main_bytes);g_vit.qkv_out=gpu(qkv_bytes);g_vit.q=gpu(qv_bytes);g_vit.k=gpu(qv_bytes);g_vit.v=gpu(qv_bytes);g_vit.score=gpu(score_bytes);g_vit.prob=gpu(score_bytes);g_vit.attention=gpu(qv_bytes);g_vit.attention_input=gpu(main_bytes);g_vit.projection_raw=gpu(main_bytes);
    std::vector<ID3D12Resource*> uploads;g_vit.down=upload_runtime_resource(read_runtime_file(L"block30-pool-identity.bin"),uploads);g_vit.enter=upload_runtime_resource(read_runtime_file(L"block30-enter-512x1024.bin"),uploads);g_vit.contract_weight=upload_runtime_resource(read_runtime_file(L"block31-vit-contract.f16"),uploads);g_vit.contract_skip=upload_runtime_resource(read_runtime_file(L"block31-vit-contract-skip.f16"),uploads);g_vit.projection_weight=upload_runtime_resource(read_runtime_file(L"block31-vit-projection.f16"),uploads);g_vit.projection_skip=upload_runtime_resource(read_runtime_file(L"block31-vit-projection-skip.f16"),uploads);
    for(UINT i=0;i<L;i++){wchar_t name[128];swprintf(name,128,L"block%u-vit-expand-effective.f16",31+i);g_vit.expand_weight[i]=upload_runtime_resource(read_runtime_file(name),uploads);swprintf(name,128,L"block%u-qkv-directml.f16",31+i);g_vit.qkv_weight[i]=upload_runtime_resource(read_runtime_file(name),uploads);swprintf(name,128,L"block%u-qkv-scales.f32",31+i);g_vit.scale[i]=upload_runtime_resource(read_runtime_file(name),uploads);}
    D3D12_RESOURCE_BARRIER constants[2]{};ID3D12Resource*cr[2]={g_vit.down,g_vit.enter};for(UINT i=0;i<2;i++){constants[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;constants[i].Transition.pResource=cr[i];constants[i].Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;constants[i].Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;constants[i].Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}g_list->ResourceBarrier(2,constants);
    g_vit.crop.Create(g_device,g_s512.main[0],g_vit.cropped);g_vit.predown.Create(g_device,g_vit.down,g_vit.enter,g_vit.cropped,g_vit.predown_mid,g_vit.source[0]);g_vit.softmax.Create(g_device,g_vit.score,g_vit.prob,score_bytes);
    for(UINT p=0;p<2;p++){g_vit.front[p].Create(g_device,g_vit.source[p],g_vit.branch,g_vit.contract_raw,g_vit.contract_skip,g_vit.main,g_vit.contract_input,g_vit.hidden,main_bytes,branch_bytes);g_vit.output[p].Create(g_device,g_vit.attention,g_vit.projection_raw,g_vit.hidden,g_vit.projection_skip,g_vit.attention_input,g_vit.source[p^1],main_bytes);}
    for(UINT i=0;i<L;i++){g_vit.pack[i].Create(g_device,g_vit.qkv_out,g_vit.scale[i],g_vit.q,g_vit.k,g_vit.v,qkv_bytes,qv_bytes);g_vit.expand[i].Bind(g_vit.main,g_vit.expand_weight[i],g_vit.branch);g_vit.contract[i].Bind(g_vit.contract_input,g_vit.contract_weight,g_vit.contract_raw);g_vit.qkv[i].Bind(g_vit.hidden,g_vit.qkv_weight[i],g_vit.qkv_out);g_vit.qk[i].Bind(g_vit.q,g_vit.k,g_vit.score);g_vit.av[i].Bind(g_vit.prob,g_vit.v,g_vit.attention);g_vit.projection[i].Bind(g_vit.attention_input,g_vit.projection_weight,g_vit.projection_raw);}
    dmlrt_check("vit init close",g_list->Close());ID3D12CommandList*lists[]={g_list};g_queue->ExecuteCommandLists(1,lists);dmlrt_check("vit init signal",g_queue->Signal(g_fence,8));dmlrt_check("vit init event",g_fence->SetEventOnCompletion(8,g_event));if(WaitForSingleObject(g_event,30000)!=WAIT_OBJECT_0)throw DmlFailure{"vit init wait",HRESULT_FROM_WIN32(ERROR_TIMEOUT)};for(auto*r:uploads)r->Release();g_vit_ready.store(true);log("blocks31_38_ready input=%p output=%p tokens=%u\n",g_s512.main[0],g_vit.source[0],T);
}

void initialize_blocks39_47(){
    constexpr UINT L=8,T=2560,C=512,H=256,Q=768,A=256;dmlrt_check("d512 allocator reset",g_allocator->Reset());dmlrt_check("d512 list reset",g_list->Reset(g_allocator,nullptr));g_d512.prefix.Create(g_dml,g_device,1,2040,1536,512);g_d512.prefix.RecordInitialization(g_recorder,g_list);
    for(UINT i=0;i<L;i++){g_d512.gate[i].Create(g_dml,g_device,1,T,C,H);g_d512.up[i].Create(g_dml,g_device,1,T,C,H);g_d512.project[i].Create(g_dml,g_device,1,T,H,C);g_d512.qkv[i].Create(g_dml,g_device,1,T,C,Q);g_d512.attention_project[i].Create(g_dml,g_device,1,T,A,C);for(auto*op:{&g_d512.gate[i],&g_d512.up[i],&g_d512.project[i],&g_d512.qkv[i],&g_d512.attention_project[i]})op->RecordInitialization(g_recorder,g_list);}
    auto gpu=[&](UINT64 n){return make_buffer(n,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);};const UINT64 main_bytes=UINT64(T)*C*2,hidden_bytes=UINT64(T)*H*2,qkv_bytes=UINT64(T)*Q*2;
    g_d512.main[0]=gpu(main_bytes);g_d512.main[1]=gpu(main_bytes);g_d512.combined=gpu(2040ull*1536*2);g_d512.prefix_raw=gpu(2040ull*512*2);g_d512.gate_out=gpu(hidden_bytes);g_d512.up_out=gpu(hidden_bytes);g_d512.hidden=gpu(hidden_bytes);g_d512.project_raw=gpu(main_bytes);g_d512.feature=gpu(main_bytes);g_d512.qkv_raw=gpu(qkv_bytes);g_d512.qkv_float=gpu(qkv_bytes*2);g_d512.attention_float=gpu(hidden_bytes*2);g_d512.attention=gpu(hidden_bytes);g_d512.attention_raw=gpu(main_bytes);
    std::vector<ID3D12Resource*>uploads;g_d512.prefix_weight=upload_runtime_resource(read_runtime_file(L"block39-directml-matrix.f16"),uploads);g_d512.prefix_bias=upload_runtime_resource(read_runtime_file(L"block39-directml-bias.f32"),uploads);const wchar_t*parts[9]={L"gate.f16",L"up.f16",L"project.f16",L"qkv.f16",L"attention_project.f16",L"ffn_skip.f32",L"attention_skip.f32",L"attention_bias.f32",L"attention_scale.f32"};
    for(UINT i=0;i<L;i++){ID3D12Resource**dest[9]={&g_d512.gw[i],&g_d512.uw[i],&g_d512.pw[i],&g_d512.qw[i],&g_d512.aw[i],&g_d512.fs[i],&g_d512.as[i],&g_d512.bias[i],&g_d512.scale[i]};for(UINT j=0;j<9;j++){wchar_t name[128];swprintf(name,128,L"block%u-logical-effective-%ls",40+i,parts[j]);*dest[j]=upload_runtime_resource(read_runtime_file(name),uploads);}}
    g_d512.prefix_pass.Create(g_device,g_vit.source[0],g_s512.main[0],g_d512.prefix_raw,g_d512.prefix_bias,g_d512.combined,g_d512.main[0]);g_d512.prefix.Bind(g_d512.combined,g_d512.prefix_weight,g_d512.prefix_raw);const UINT shifts[L]={1,3,2,0,1,3,2,0};for(UINT i=0;i<L;i++){UINT p=i&1,n=p^1;g_d512.gate[i].Bind(g_d512.main[p],g_d512.gw[i],g_d512.gate_out);g_d512.up[i].Bind(g_d512.main[p],g_d512.uw[i],g_d512.up_out);g_d512.project[i].Bind(g_d512.hidden,g_d512.pw[i],g_d512.project_raw);g_d512.qkv[i].Bind(g_d512.feature,g_d512.qw[i],g_d512.qkv_raw);g_d512.attention_project[i].Bind(g_d512.attention,g_d512.aw[i],g_d512.attention_raw);g_d512.boundary[i].Create(g_device,g_d512.gate_out,g_d512.up_out,g_d512.project_raw,g_d512.main[p],g_d512.fs[i],g_d512.attention_raw,g_d512.feature,g_d512.as[i],g_d512.hidden,g_d512.feature,g_d512.main[n]);g_d512.window[i].Create(g_device,g_d512.qkv_raw,g_d512.qkv_float,g_d512.bias[i],g_d512.scale[i],g_d512.attention_float,g_d512.attention,shifts[i]);}
    dmlrt_check("d512 init close",g_list->Close());ID3D12CommandList*lists[]={g_list};g_queue->ExecuteCommandLists(1,lists);dmlrt_check("d512 init signal",g_queue->Signal(g_fence,9));dmlrt_check("d512 init event",g_fence->SetEventOnCompletion(9,g_event));if(WaitForSingleObject(g_event,30000)!=WAIT_OBJECT_0)throw DmlFailure{"d512 init wait",HRESULT_FROM_WIN32(ERROR_TIMEOUT)};for(auto*r:uploads)r->Release();g_d512_ready.store(true);log("blocks39_47_ready vit=%p skip=%p output=%p\n",g_vit.source[0],g_s512.main[0],g_d512.main[0]);
}

void initialize_blocks48_55(){
    constexpr UINT L=8,T=8640,C=256,H=288,Q=384,A=128;dmlrt_check("d256 allocator reset",g_allocator->Reset());dmlrt_check("d256 list reset",g_list->Reset(g_allocator,nullptr));g_d256.prefix.Create(g_dml,g_device,1,T,512,256);g_d256.prefix.RecordInitialization(g_recorder,g_list);for(UINT i=0;i<L;i++){g_d256.gate[i].Create(g_dml,g_device,1,T,C,H);g_d256.up[i].Create(g_dml,g_device,1,T,C,H);g_d256.project[i].Create(g_dml,g_device,1,T,H,C);g_d256.qkv[i].Create(g_dml,g_device,1,T,C,Q);g_d256.attention_project[i].Create(g_dml,g_device,1,T,A,C);for(auto*op:{&g_d256.gate[i],&g_d256.up[i],&g_d256.project[i],&g_d256.qkv[i],&g_d256.attention_project[i]})op->RecordInitialization(g_recorder,g_list);}
    auto gpu=[&](UINT64 n){return make_buffer(n,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);};const UINT64 main_bytes=UINT64(T)*C*2,hidden_bytes=UINT64(T)*H*2,qkv_bytes=UINT64(T)*Q*2,attention_bytes=UINT64(T)*A*2;g_d256.main[0]=gpu(main_bytes);g_d256.main[1]=gpu(main_bytes);g_d256.packed=gpu(UINT64(T)*512*2);g_d256.prefix_raw=gpu(main_bytes);g_d256.gate_out=gpu(hidden_bytes);g_d256.up_out=gpu(hidden_bytes);g_d256.hidden=gpu(hidden_bytes);g_d256.project_raw=gpu(main_bytes);g_d256.feature=gpu(main_bytes);g_d256.qkv_raw=gpu(qkv_bytes);g_d256.qkv_float=gpu(qkv_bytes*2);g_d256.attention_float=gpu(attention_bytes*2);g_d256.attention=gpu(attention_bytes);g_d256.attention_raw=gpu(main_bytes);
    std::vector<ID3D12Resource*>uploads;g_d256.prefix_weight=upload_runtime_resource(read_runtime_file(L"block48-prefix-matrix.f16"),uploads);g_d256.prefix_bias=upload_runtime_resource(read_runtime_file(L"block48-prefix-bias.f32"),uploads);const wchar_t*parts[9]={L"expand.f16",L"up.f16",L"project.f16",L"qkv.f16",L"attention_project.f16",L"ffn_skip.f32",L"attention_skip.f32",L"bias.f32",L"scale.f32"};for(UINT i=0;i<L;i++){ID3D12Resource**dest[9]={&g_d256.gw[i],&g_d256.uw[i],&g_d256.pw[i],&g_d256.qw[i],&g_d256.aw[i],&g_d256.fs[i],&g_d256.as[i],&g_d256.bias[i],&g_d256.scale[i]};for(UINT j=0;j<9;j++){wchar_t name[128];swprintf(name,128,L"block%u-body-effective-%ls",48+i,parts[j]);*dest[j]=upload_runtime_resource(read_runtime_file(name),uploads);}}
    g_d256.prefix_pass.Create(g_device,g_d512.main[0],g_s256.main[0],g_d256.prefix_raw,g_d256.prefix_bias,g_d256.packed,g_d256.main[0]);g_d256.prefix.Bind(g_d256.packed,g_d256.prefix_weight,g_d256.prefix_raw);const UINT shifts[L]={0,0,1,3,2,0,1,3};for(UINT i=0;i<L;i++){UINT p=i&1,n=p^1;g_d256.gate[i].Bind(g_d256.main[p],g_d256.gw[i],g_d256.gate_out);g_d256.up[i].Bind(g_d256.main[p],g_d256.uw[i],g_d256.up_out);g_d256.project[i].Bind(g_d256.hidden,g_d256.pw[i],g_d256.project_raw);g_d256.qkv[i].Bind(g_d256.feature,g_d256.qw[i],g_d256.qkv_raw);g_d256.attention_project[i].Bind(g_d256.attention,g_d256.aw[i],g_d256.attention_raw);g_d256.boundary[i].Create(g_device,g_d256.gate_out,g_d256.up_out,g_d256.project_raw,g_d256.main[p],g_d256.fs[i],g_d256.attention_raw,g_d256.feature,g_d256.as[i],g_d256.hidden,g_d256.feature,g_d256.main[n]);g_d256.window[i].Create(g_device,g_d256.qkv_raw,g_d256.qkv_float,g_d256.bias[i],g_d256.scale[i],g_d256.attention_float,g_d256.attention,shifts[i]);}
    dmlrt_check("d256 init close",g_list->Close());ID3D12CommandList*lists[]={g_list};g_queue->ExecuteCommandLists(1,lists);dmlrt_check("d256 init signal",g_queue->Signal(g_fence,10));dmlrt_check("d256 init event",g_fence->SetEventOnCompletion(10,g_event));if(WaitForSingleObject(g_event,30000)!=WAIT_OBJECT_0)throw DmlFailure{"d256 init wait",HRESULT_FROM_WIN32(ERROR_TIMEOUT)};for(auto*r:uploads)r->Release();g_d256_ready.store(true);log("blocks48_55_ready low=%p skip=%p output=%p\n",g_d512.main[0],g_s256.main[0],g_d256.main[0]);
}

void initialize_blocks56_61(){
    constexpr UINT L=6,T=32640,C=128,H=160,Q=192,A=64;dmlrt_check("d128 allocator reset",g_allocator->Reset());dmlrt_check("d128 list reset",g_list->Reset(g_allocator,nullptr));g_d128.prefix.Create(g_dml,g_device,1,T,256,128);g_d128.prefix.RecordInitialization(g_recorder,g_list);for(UINT i=0;i<L;i++){g_d128.gate[i].Create(g_dml,g_device,1,T,C,H);g_d128.up[i].Create(g_dml,g_device,1,T,C,H);g_d128.project[i].Create(g_dml,g_device,1,T,H,C);g_d128.qkv[i].Create(g_dml,g_device,1,T,C,Q);g_d128.attention_project[i].Create(g_dml,g_device,1,T,A,C);for(auto*op:{&g_d128.gate[i],&g_d128.up[i],&g_d128.project[i],&g_d128.qkv[i],&g_d128.attention_project[i]})op->RecordInitialization(g_recorder,g_list);}
    auto gpu=[&](UINT64 n){return make_buffer(n,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);};const UINT64 main_bytes=UINT64(T)*C*2,hidden_bytes=UINT64(T)*H*2,qkv_bytes=UINT64(T)*Q*2,attention_bytes=UINT64(T)*A*2;g_d128.main[0]=gpu(main_bytes);g_d128.main[1]=gpu(main_bytes);g_d128.packed=gpu(UINT64(T)*256*2);g_d128.prefix_raw=gpu(main_bytes);g_d128.gate_out=gpu(hidden_bytes);g_d128.up_out=gpu(hidden_bytes);g_d128.hidden=gpu(hidden_bytes);g_d128.project_raw=gpu(main_bytes);g_d128.feature=gpu(main_bytes);g_d128.qkv_raw=gpu(qkv_bytes);g_d128.qkv_float=gpu(qkv_bytes*2);g_d128.attention_float=gpu(attention_bytes*2);g_d128.attention=gpu(attention_bytes);g_d128.attention_raw=gpu(main_bytes);
    std::vector<ID3D12Resource*>uploads;g_d128.prefix_weight=upload_runtime_resource(read_runtime_file(L"block56-prefix-directml.f16"),uploads);g_d128.prefix_bias=upload_runtime_resource(read_runtime_file(L"block56-prefix-bias.f32"),uploads);const wchar_t*parts[9]={L"expand.f16",L"up.f16",L"project.f16",L"qkv.f16",L"attention_project.f16",L"ffn_skip.f32",L"attention_skip.f32",L"bias.f32",L"scale.f32"};for(UINT i=0;i<L;i++){ID3D12Resource**dest[9]={&g_d128.gw[i],&g_d128.uw[i],&g_d128.pw[i],&g_d128.qw[i],&g_d128.aw[i],&g_d128.fs[i],&g_d128.as[i],&g_d128.bias[i],&g_d128.scale[i]};for(UINT j=0;j<9;j++){wchar_t name[128];swprintf(name,128,L"block%u-body-effective-%ls",56+i,parts[j]);*dest[j]=upload_runtime_resource(read_runtime_file(name),uploads);}}
    g_d128.prefix_pass.Create(g_device,g_d256.main[0],g_s128.main[0],g_d128.prefix_raw,g_d128.prefix_bias,g_d128.packed,g_d128.main[0]);g_d128.prefix.Bind(g_d128.packed,g_d128.prefix_weight,g_d128.prefix_raw);const UINT shifts[L]={0,0,1,3,2,0};for(UINT i=0;i<L;i++){UINT p=i&1,n=p^1;g_d128.gate[i].Bind(g_d128.main[p],g_d128.gw[i],g_d128.gate_out);g_d128.up[i].Bind(g_d128.main[p],g_d128.uw[i],g_d128.up_out);g_d128.project[i].Bind(g_d128.hidden,g_d128.pw[i],g_d128.project_raw);g_d128.qkv[i].Bind(g_d128.feature,g_d128.qw[i],g_d128.qkv_raw);g_d128.attention_project[i].Bind(g_d128.attention,g_d128.aw[i],g_d128.attention_raw);g_d128.boundary[i].Create(g_device,g_d128.gate_out,g_d128.up_out,g_d128.project_raw,g_d128.main[p],g_d128.fs[i],g_d128.attention_raw,g_d128.feature,g_d128.as[i],g_d128.hidden,g_d128.feature,g_d128.main[n]);g_d128.window[i].Create(g_device,g_d128.qkv_raw,g_d128.qkv_float,g_d128.bias[i],g_d128.scale[i],g_d128.attention_float,g_d128.attention,shifts[i]);}
    dmlrt_check("d128 init close",g_list->Close());ID3D12CommandList*lists[]={g_list};g_queue->ExecuteCommandLists(1,lists);dmlrt_check("d128 init signal",g_queue->Signal(g_fence,11));dmlrt_check("d128 init event",g_fence->SetEventOnCompletion(11,g_event));if(WaitForSingleObject(g_event,30000)!=WAIT_OBJECT_0)throw DmlFailure{"d128 init wait",HRESULT_FROM_WIN32(ERROR_TIMEOUT)};for(auto*r:uploads)r->Release();g_d128_ready.store(true);log("blocks56_61_ready low=%p skip=%p output=%p\n",g_d256.main[0],g_s128.main[0],g_d128.main[0]);
}

void initialize_blocks62_65(){
    constexpr UINT L=4,T=130560,C=64,H=96,Q=96,A=32;dmlrt_check("d64 allocator reset",g_allocator->Reset());dmlrt_check("d64 list reset",g_list->Reset(g_allocator,nullptr));g_d64.prefix.Create(g_dml,g_device,1,T,128,64);g_d64.prefix.RecordInitialization(g_recorder,g_list);for(UINT i=0;i<L;i++){g_d64.gate[i].Create(g_dml,g_device,1,T,C,H);g_d64.up[i].Create(g_dml,g_device,1,T,C,H);g_d64.project[i].Create(g_dml,g_device,1,T,H,C);g_d64.qkv[i].Create(g_dml,g_device,1,T,C,Q);g_d64.attention_project[i].Create(g_dml,g_device,1,T,A,C);for(auto*op:{&g_d64.gate[i],&g_d64.up[i],&g_d64.project[i],&g_d64.qkv[i],&g_d64.attention_project[i]})op->RecordInitialization(g_recorder,g_list);}
    auto gpu=[&](UINT64 n){return make_buffer(n,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);};const UINT64 main_bytes=UINT64(T)*C*2,hidden_bytes=UINT64(T)*H*2,qkv_bytes=UINT64(T)*Q*2,attention_bytes=UINT64(T)*A*2;g_d64.main[0]=gpu(main_bytes);g_d64.main[1]=gpu(main_bytes);g_d64.packed=gpu(UINT64(T)*128*2);g_d64.prefix_raw=gpu(main_bytes);g_d64.gate_out=gpu(hidden_bytes);g_d64.up_out=gpu(hidden_bytes);g_d64.hidden=gpu(hidden_bytes);g_d64.project_raw=gpu(main_bytes);g_d64.feature=gpu(main_bytes);g_d64.qkv_raw=gpu(qkv_bytes);g_d64.qkv_float=gpu(qkv_bytes*2);g_d64.attention_float=gpu(attention_bytes*2);g_d64.attention=gpu(attention_bytes);g_d64.attention_raw=gpu(main_bytes);
    std::vector<ID3D12Resource*>uploads;g_d64.prefix_weight=upload_runtime_resource(read_runtime_file(L"block62-prefix-matrix.f16"),uploads);g_d64.prefix_bias=upload_runtime_resource(read_runtime_file(L"block62-prefix-bias.f32"),uploads);const wchar_t*parts[9]={L"expand.f16",L"up.f16",L"project.f16",L"qkv.f16",L"attention_project.f16",L"ffn_skip.f32",L"attention_skip.f32",L"bias.f32",L"scale.f32"};for(UINT i=0;i<L;i++){ID3D12Resource**dest[9]={&g_d64.gw[i],&g_d64.uw[i],&g_d64.pw[i],&g_d64.qw[i],&g_d64.aw[i],&g_d64.fs[i],&g_d64.as[i],&g_d64.bias[i],&g_d64.scale[i]};for(UINT j=0;j<9;j++){wchar_t name[128];swprintf(name,128,L"block%u-body-effective-%ls",62+i,parts[j]);*dest[j]=upload_runtime_resource(read_runtime_file(name),uploads);}}
    g_d64.prefix_pass.Create(g_device,g_d128.main[0],g_s64_main[0],g_d64.prefix_raw,g_d64.prefix_bias,g_d64.packed,g_d64.main[0]);g_d64.prefix.Bind(g_d64.packed,g_d64.prefix_weight,g_d64.prefix_raw);const UINT shifts[L]={0,0,1,3};for(UINT i=0;i<L;i++){UINT p=i&1,n=p^1;g_d64.gate[i].Bind(g_d64.main[p],g_d64.gw[i],g_d64.gate_out);g_d64.up[i].Bind(g_d64.main[p],g_d64.uw[i],g_d64.up_out);g_d64.project[i].Bind(g_d64.hidden,g_d64.pw[i],g_d64.project_raw);g_d64.qkv[i].Bind(g_d64.feature,g_d64.qw[i],g_d64.qkv_raw);g_d64.attention_project[i].Bind(g_d64.attention,g_d64.aw[i],g_d64.attention_raw);g_d64.boundary[i].Create(g_device,g_d64.gate_out,g_d64.up_out,g_d64.project_raw,g_d64.main[p],g_d64.fs[i],g_d64.attention_raw,g_d64.feature,g_d64.as[i],g_d64.hidden,g_d64.feature,g_d64.main[n]);g_d64.window[i].Create(g_device,g_d64.qkv_raw,g_d64.qkv_float,g_d64.bias[i],g_d64.scale[i],g_d64.attention_float,g_d64.attention,shifts[i]);}
    dmlrt_check("d64 init close",g_list->Close());ID3D12CommandList*lists[]={g_list};g_queue->ExecuteCommandLists(1,lists);dmlrt_check("d64 init signal",g_queue->Signal(g_fence,12));dmlrt_check("d64 init event",g_fence->SetEventOnCompletion(12,g_event));if(WaitForSingleObject(g_event,30000)!=WAIT_OBJECT_0)throw DmlFailure{"d64 init wait",HRESULT_FROM_WIN32(ERROR_TIMEOUT)};for(auto*r:uploads)r->Release();g_d64_ready.store(true);log("blocks62_65_ready low=%p skip=%p output=%p\n",g_d128.main[0],g_s64_main[0],g_d64.main[0]);
}

void initialize_blocks66_69(){
    constexpr UINT T=522240;constexpr UINT64 fp32_bytes=UINT64(T)*32*4,half_bytes=UINT64(T)*32*2,qkv_bytes=UINT64(T)*48*4;dmlrt_check("d32 allocator reset",g_allocator->Reset());dmlrt_check("d32 list reset",g_list->Reset(g_allocator,nullptr));g_d32.prefix.Create(g_dml,g_device,1,T,64,32);g_d32.prefix.RecordInitialization(g_recorder,g_list);auto gpu=[&](UINT64 n){return make_buffer(n,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);};g_d32.packed=gpu(UINT64(T)*64*2);g_d32.raw=gpu(half_bytes);g_d32.half=gpu(half_bytes);g_d32.prefix_fp32=gpu(fp32_bytes);g_d32.main[0]=gpu(fp32_bytes);g_d32.main[1]=gpu(fp32_bytes);g_d32.feature=gpu(fp32_bytes);g_d32.qkv=gpu(qkv_bytes);
    std::vector<ID3D12Resource*>uploads;g_d32.weight=upload_runtime_resource(read_runtime_file(L"block66-prefix-matrix.f16"),uploads);g_d32.bias=upload_runtime_resource(read_runtime_file(L"block66-prefix-bias.f32"),uploads);for(UINT i=0;i<4;i++)g_d32.body_weights[i]=upload_runtime_resource(read_runtime_file((std::wstring(L"block")+std::to_wstring(66+i)+L"-body-effective.bin").c_str()),uploads);
    g_d32.prefix_pass.Create(g_device,g_d64.main[0],g_front_main[1],g_d32.raw,g_d32.bias,g_d32.packed,g_d32.half);g_d32.prefix.Bind(g_d32.packed,g_d32.weight,g_d32.raw);g_d32.bridge.Create(g_device,g_d32.half,g_d32.prefix_fp32,T*32);
    D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,4}};D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.DescriptorTable={2,ranges};D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&parameter;ID3DBlob*sig=nullptr,*error=nullptr;dmlrt_check("d32 signature",D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&error));dmlrt_check("d32 root",g_device->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&g_d32.root)));const UINT shifts[4]={0,1,1,0},versions[3]={1,2,2};const wchar_t*passes[3]={L"ffn",L"qkv",L"attention"};for(UINT b=0;b<4;b++)for(UINT p=0;p<3;p++){wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,LR"(D:\DLSSNR-Lab\shader-cache\block1-v%u-960x544-s%u-%ls.cso)",versions[p],shifts[b],passes[p]);ID3DBlob*code=nullptr;dmlrt_check("d32 shader",D3DReadFileToBlob(path,&code));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=g_d32.root;pd.CS={code->GetBufferPointer(),code->GetBufferSize()};dmlrt_check("d32 pso",g_device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&g_d32.pso[b][p])));}
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,28,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};dmlrt_check("d32 heap",g_device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&g_d32.heap)));UINT step=g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto cpu=g_d32.heap->GetCPUDescriptorHandleForHeapStart();for(UINT b=0;b<4;b++){ID3D12Resource*source=b?g_d32.main[(b-1)&1]:g_d32.prefix_fp32,*dest=g_d32.main[b&1];D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Format=DXGI_FORMAT_R32_TYPELESS;sv.Buffer.NumElements=41220/4;sv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;g_device->CreateShaderResourceView(g_d32.body_weights[b],&sv,cpu);cpu.ptr+=step;sv.Format=DXGI_FORMAT_UNKNOWN;sv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_NONE;sv.Buffer.StructureByteStride=4;for(auto x:{std::pair<ID3D12Resource*,UINT64>{source,fp32_bytes},{g_d32.feature,fp32_bytes},{g_d32.qkv,qkv_bytes}}){sv.Buffer.NumElements=(UINT)(x.second/4);g_device->CreateShaderResourceView(x.first,&sv,cpu);cpu.ptr+=step;}D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Buffer.StructureByteStride=4;for(auto x:{std::pair<ID3D12Resource*,UINT64>{g_d32.feature,fp32_bytes},{dest,fp32_bytes},{g_d32.qkv,qkv_bytes}}){uv.Buffer.NumElements=(UINT)(x.second/4);g_device->CreateUnorderedAccessView(x.first,nullptr,&uv,cpu);cpu.ptr+=step;}}
    dmlrt_check("d32 init close",g_list->Close());ID3D12CommandList*lists[]={g_list};g_queue->ExecuteCommandLists(1,lists);dmlrt_check("d32 init signal",g_queue->Signal(g_fence,13));dmlrt_check("d32 init event",g_fence->SetEventOnCompletion(13,g_event));if(WaitForSingleObject(g_event,30000)!=WAIT_OBJECT_0)throw DmlFailure{"d32 init wait",HRESULT_FROM_WIN32(ERROR_TIMEOUT)};for(auto*r:uploads)r->Release();g_d32_ready.store(true);log("blocks66_69_ready low=%p skip=%p output=%p\n",g_d64.main[0],g_front_main[1],g_d32.main[1]);
}

void initialize_block70(){
    constexpr UINT W=1920,H=1088,T=W*H,TILES=240*136;constexpr UINT64 prefix_bytes=UINT64(TILES)*2048*4,body_bytes=UINT64(T)*32*4,qkv_bytes=UINT64(T)*48*4,pack_bytes=1920ull*1080*4;dmlrt_check("b70 allocator reset",g_allocator->Reset());dmlrt_check("b70 list reset",g_list->Reset(g_allocator,nullptr));std::vector<ID3D12Resource*>uploads;auto sparse_data=read_runtime_file(L"block70-prefix-sparse.bin");if(sparse_data.size()<8200)throw DmlFailure{"b70 sparse size",E_INVALIDARG};g_b70.nnz=*reinterpret_cast<const UINT*>(sparse_data.data()+8192);g_b70.sparse=upload_runtime_resource(sparse_data,uploads);g_b70.body_weight=upload_runtime_resource(read_runtime_file(L"block70-body-compatible.bin"),uploads);g_b70.out_weight=upload_runtime_resource(read_runtime_file(L"block70-outconv-effective.bin"),uploads);auto gpu=[&](UINT64 n){return make_buffer(n,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);};g_b70.prefix=gpu(prefix_bytes);g_b70.feature=gpu(body_bytes);g_b70.qkv=gpu(qkv_bytes);g_b70.body=gpu(body_bytes);g_b70.packed=gpu(pack_bytes);
    auto root=[&](UINT ns,UINT nu){D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,ns,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,nu,0,0,ns}};D3D12_ROOT_PARAMETER p{};p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p.DescriptorTable={2,ranges};D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&p;ID3DBlob*sig=nullptr,*error=nullptr;dmlrt_check("b70 signature",D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&error));ID3D12RootSignature*r=nullptr;dmlrt_check("b70 root",g_device->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&r)));return r;};auto compile=[&](ID3D12RootSignature*r,const char*s,const char*entry,const D3D_SHADER_MACRO*macros=nullptr){ID3DBlob*code=nullptr,*error=nullptr;dmlrt_check("b70 compile",D3DCompile(s,std::strlen(s),nullptr,macros,nullptr,entry,"cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=r;pd.CS={code->GetBufferPointer(),code->GetBufferSize()};ID3D12PipelineState*p=nullptr;dmlrt_check("b70 pso",g_device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&p)));return p;};
    g_b70.prefix_root=root(3,1);const char prefix_shader[]=R"(ByteAddressBuffer p:register(t0);StructuredBuffer<float>mainv:register(t1),skipv:register(t2);RWStructuredBuffer<float>y:register(u0);uint lid(uint3 g,uint3 t){return(g.y*65535+g.x)*64+t.x;}float X(uint sample,uint i){uint tx=sample%240,ty=sample/240,s=i&511,plane=s/32,c=s%32,x=tx*4+plane%4,y=ty*4+plane/4;return i<512?mainv[(y*960+x)*32+c]:skipv[(y*960+x)*32+c];}[numthreads(64,1,1)]void main(uint3 g:SV_GroupID,uint3 t:SV_GroupThreadID){uint z=lid(g,t);if(z>=32640*2048)return;uint sample=z/2048,o=z%2048,b=p.Load(o*4),e=p.Load((o+1)*4),ibase=8200,wbase=8200+NNZ*4;float v=0;[loop]for(uint k=b;k<e;k++){uint i=p.Load(ibase+k*4);v+=X(sample,i)*asfloat(p.Load(wbase+k*4));}y[z]=v;})";char nnz[16];std::snprintf(nnz,sizeof(nnz),"%u",g_b70.nnz);D3D_SHADER_MACRO pm[]={{"NNZ",nnz},{nullptr,nullptr}};g_b70.prefix_pso=compile(g_b70.prefix_root,prefix_shader,"main",pm);
    g_b70.body_root=root(4,3);const char body_shader[]=R"(ByteAddressBuffer w:register(t0);StructuredBuffer<float>inp:register(t1),feat_in:register(t2),qkv_in:register(t3);RWStructuredBuffer<float>feat:register(u0),outp:register(u1),qkv_out:register(u2);uint lid(uint3 g,uint3 t){return(g.y*65535+g.x)*64+t.x;}float W(uint i){return asfloat(w.Load(i*4));}uint I(uint t,uint c){uint x=t%1920,y=t/1920,tile=(y/8)*240+x/8,local=((y%8)*8+x%8)*32+c;return tile*2048+local;}float F(float x){if(x==0)return 0;float s=x<0?-1:1,a=abs(x);if(a<.015625)return s*round(a*512)/512;float e=clamp(floor(log2(a)),-6.,8.),m=round((a/exp2(e)-1)*8);if(m>=8){m=0;e++;}return s*min(exp2(e)*(1+m/8),448.);}[numthreads(64,1,1)]void ffn(uint3 g:SV_GroupID,uint3 q:SV_GroupThreadID){uint t=lid(g,q);if(t>=1920*1088)return;float h[64];[loop]for(uint j=0;j<64;j++){float a=0;[loop]for(uint c=0;c<32;c++)a+=inp[I(t,c)]*W(j*32+c);a=clamp(a,-4.,4.);h[j]=a*(.89453125+a*(.447265625-.055908203125*abs(a)));}[loop]for(uint c=0;c<32;c++){float v=0;[loop]for(uint j=0;j<64;j++)v+=h[j]*W(2048+c*64+j);feat[t*32+c]=F(v+inp[I(t,c)]*W(10241+c));}}[numthreads(64,1,1)]void qkv(uint3 g:SV_GroupID,uint3 q:SV_GroupThreadID){uint t=lid(g,q);if(t>=1920*1088)return;[loop]for(uint o=0;o<16;o++){float a=0,b=0,c=0;[loop]for(uint j=0;j<16;j++){float e=feat_in[t*32+j*2],z=feat_in[t*32+j*2+1];a+=e*W(4096+o*16+j)+z*W(4352+o*16+j);b+=e*W(4608+o*16+j)+z*W(4864+o*16+j);c+=e*W(5120+o*16+j)+z*W(5376+o*16+j);}qkv_out[t*48+o]=a;qkv_out[t*48+16+o]=b;qkv_out[t*48+32+o]=c;}}[numthreads(64,1,1)]void attention(uint3 g:SV_GroupID,uint3 q:SV_GroupThreadID){uint t=lid(g,q);if(t>=1920*1088)return;uint x=t%1920,y=t/1920,wx=x&~7,wy=y&~7,qi=(y%8)*8+x%8;float qv[16],qq=0;[unroll]for(uint d=0;d<16;d++){qv[d]=qkv_in[t*48+d];qq+=qv[d]*qv[d];}float mx=-3.4e38;[loop]for(uint k=0;k<64;k++){uint kt=(wy+k/8)*1920+wx+k%8,ko=kt*48+16;float kk=0,dot=0;[unroll]for(uint d=0;d<16;d++){float z=qkv_in[ko+d];kk+=z*z;dot+=qv[d]*z;}mx=max(mx,dot*rsqrt(max(qq,1e-12))*rsqrt(max(kk,1e-12))*W(10240)+W(6144+qi*64+k));}float den=0,a[16];[unroll]for(uint d=0;d<16;d++)a[d]=0;[loop]for(uint k=0;k<64;k++){uint kt=(wy+k/8)*1920+wx+k%8,ko=kt*48+16;float kk=0,dot=0;[unroll]for(uint d=0;d<16;d++){float z=qkv_in[ko+d];kk+=z*z;dot+=qv[d]*z;}float e=exp(dot*rsqrt(max(qq,1e-12))*rsqrt(max(kk,1e-12))*W(10240)+W(6144+qi*64+k)-mx);den+=e;[unroll]for(uint d=0;d<16;d++)a[d]+=e*qkv_in[ko+16+d];}[loop]for(uint c=0;c<32;c++){float z=0;[loop]for(uint d=0;d<16;d++)z+=(a[d]/den)*W(5632+c*16+d);outp[t*32+c]=F(z+feat_in[t*32+c]*W(10273+c));}})";g_b70.body_pso[0]=compile(g_b70.body_root,body_shader,"ffn");g_b70.body_pso[1]=compile(g_b70.body_root,body_shader,"qkv");g_b70.body_pso[2]=compile(g_b70.body_root,body_shader,"attention");
    D3D12_DESCRIPTOR_RANGE output_ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,2}};D3D12_ROOT_PARAMETER output_params[2]{};output_params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;output_params[0].DescriptorTable={2,output_ranges};output_params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;output_params[1].Constants={0,0,1};D3D12_ROOT_SIGNATURE_DESC output_rd{};output_rd.NumParameters=2;output_rd.pParameters=output_params;ID3DBlob *output_sig=nullptr,*output_error=nullptr;dmlrt_check("output signature",D3D12SerializeRootSignature(&output_rd,D3D_ROOT_SIGNATURE_VERSION_1,&output_sig,&output_error));dmlrt_check("output root",g_device->CreateRootSignature(0,output_sig->GetBufferPointer(),output_sig->GetBufferSize(),IID_PPV_ARGS(&g_b70.output_root)));const char output_shader[]=R"(cbuffer Diagnostic:register(b0){uint output_mode;}StructuredBuffer<float>w:register(t0),feature:register(t1);RWTexture2D<float4>target:register(u0);RWStructuredBuffer<uint>packed:register(u1);uint lid(uint3 g,uint3 t){return(g.y*65535+g.x)*64+t.x;}[numthreads(64,1,1)]void outconv(uint3 g:SV_GroupID,uint3 t:SV_GroupThreadID){uint p=lid(g,t);if(p>=1920*1080)return;if(output_mode==1||(output_mode==2&&p%1920<960))return;float4 base=target[uint2(p%1920,p/1920)];[unroll]for(uint c=0;c<3;c++){float r=0;[unroll]for(uint i=0;i<32;i++)r+=feature[p*32+i]*w[i*3+c];base[c]=saturate(base[c]+r);}base.a=1;target[uint2(p%1920,p/1920)]=base;}[numthreads(64,1,1)]void pack(uint3 g:SV_GroupID,uint3 t:SV_GroupThreadID){uint p=lid(g,t);if(p>=1920*1080)return;float4 v=target[uint2(p%1920,p/1920)];uint r=(uint)round(saturate(v.r)*1023),gg=(uint)round(saturate(v.g)*1023),b=(uint)round(saturate(v.b)*1023);packed[p]=r|(gg<<10)|(b<<20)|(3u<<30);})";g_b70.out_pso=compile(g_b70.output_root,output_shader,"outconv");g_b70.pack_pso=compile(g_b70.output_root,output_shader,"pack");
    UINT step=g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto heap=[&](UINT n){D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,n,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ID3D12DescriptorHeap*h=nullptr;dmlrt_check("b70 heap",g_device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&h)));return h;};auto srv=[&](ID3D12Resource*r,UINT64 bytes,D3D12_CPU_DESCRIPTOR_HANDLE&h,bool raw){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;v.Format=raw?DXGI_FORMAT_R32_TYPELESS:DXGI_FORMAT_UNKNOWN;v.Buffer.Flags=raw?D3D12_BUFFER_SRV_FLAG_RAW:D3D12_BUFFER_SRV_FLAG_NONE;v.Buffer.StructureByteStride=raw?0:4;v.Buffer.NumElements=(UINT)(bytes/4);g_device->CreateShaderResourceView(r,&v,h);h.ptr+=step;};auto uav=[&](ID3D12Resource*r,UINT64 bytes,D3D12_CPU_DESCRIPTOR_HANDLE&h){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;v.Buffer.StructureByteStride=4;v.Buffer.NumElements=(UINT)(bytes/4);g_device->CreateUnorderedAccessView(r,nullptr,&v,h);h.ptr+=step;};g_b70.prefix_heap=heap(4);auto h=g_b70.prefix_heap->GetCPUDescriptorHandleForHeapStart();srv(g_b70.sparse,sparse_data.size(),h,true);srv(g_d32.main[1],522240ull*32*4,h,false);srv(g_block0_hwc,522240ull*32*4,h,false);uav(g_b70.prefix,prefix_bytes,h);g_b70.body_heap=heap(7);h=g_b70.body_heap->GetCPUDescriptorHandleForHeapStart();srv(g_b70.body_weight,41220,h,true);srv(g_b70.prefix,prefix_bytes,h,false);srv(g_b70.feature,body_bytes,h,false);srv(g_b70.qkv,qkv_bytes,h,false);uav(g_b70.feature,body_bytes,h);uav(g_b70.body,body_bytes,h);uav(g_b70.qkv,qkv_bytes,h);for(UINT i=0;i<8;i++){g_b70.output_heap[i]=heap(4);h=g_b70.output_heap[i]->GetCPUDescriptorHandleForHeapStart();srv(g_b70.out_weight,384,h,false);srv(g_b70.body,body_bytes,h,false);h.ptr+=step;uav(g_b70.packed,pack_bytes,h);}
    D3D12_RESOURCE_BARRIER cb[3]{};ID3D12Resource*constants[3]={g_b70.sparse,g_b70.body_weight,g_b70.out_weight};for(UINT i=0;i<3;i++){cb[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;cb[i].Transition.pResource=constants[i];cb[i].Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;cb[i].Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;cb[i].Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}g_list->ResourceBarrier(3,cb);dmlrt_check("b70 init close",g_list->Close());ID3D12CommandList*lists[]={g_list};g_queue->ExecuteCommandLists(1,lists);dmlrt_check("b70 init signal",g_queue->Signal(g_fence,14));dmlrt_check("b70 init event",g_fence->SetEventOnCompletion(14,g_event));if(WaitForSingleObject(g_event,30000)!=WAIT_OBJECT_0)throw DmlFailure{"b70 init wait",HRESULT_FROM_WIN32(ERROR_TIMEOUT)};for(auto*r:uploads)r->Release();g_b70_ready.store(true);log("block70_ready tiles=%u padded=%ux%u active=1920x1080 r10=%p\n",TILES,W,H,g_b70.packed);
}

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
    for(UINT i=0;i<4;i++){g_s64_gate[i].Record(g_recorder,commands);uav(g_s64_gate_out);g_s64_boundary[i].Record(commands,0);uav(g_s64_hidden);g_s64_project[i].Record(g_recorder,commands);uav(g_s64_project_raw);g_s64_boundary[i].Record(commands,1);uav(g_s64_feature);g_s64_qkv_op[i].Record(g_recorder,commands);uav(g_s64_qkv_raw);g_s64_window[i].Record(commands,0);uav(g_s64_qkv_float);g_s64_window[i].Record(commands,1);uav(g_s64_attention_float);g_s64_window[i].Record(commands,2);uav(g_s64_attention);g_s64_attention_project[i].Record(g_recorder,commands);uav(g_s64_attention_raw);g_s64_boundary[i].Record(commands,2);uav(g_s64_main[(i&1)^1]);}
    const auto submitted=++g_s64_submissions;if(submitted==1||submitted%120==0)log("blocks5_8_submit=%llu ffx_frame=%llu block8=%p\n",submitted,frame,g_s64_main[0]);return true;
}

template<class R, UINT L>
bool record_swin_stage(R &s, ID3D12Resource *source, ID3D12GraphicsCommandList *commands,
                       unsigned long long frame, std::atomic<bool> &ready,
                       std::atomic<unsigned long long> &submissions, const char *label,
                       std::atomic<unsigned long long> *downstream=nullptr) {
    if(!commands||!ready.load())return false;
    auto uav=[&](ID3D12Resource *r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;commands->ResourceBarrier(1,&b);};
    auto transition=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES bstate){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=a;b.Transition.StateAfter=bstate;commands->ResourceBarrier(1,&b);};
    if(submissions.load()){if(downstream&&downstream->load())transition(s.main[0],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(s.input_fp32,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(s.mid,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    transition(source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);s.bridge.Record(commands);transition(s.input_fp32,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    s.predown.Record(commands,0);transition(s.mid,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);s.predown.Record(commands,1);uav(s.main[0]);
    for(UINT i=0;i<L;i++){s.gate[i].Record(g_recorder,commands);uav(s.gate_out);if constexpr(std::is_same<R,RuntimeS512>::value){s.up[i].Record(g_recorder,commands);uav(s.up_out);}s.boundary[i].Record(commands,0);uav(s.hidden);s.project[i].Record(g_recorder,commands);uav(s.project_raw);s.boundary[i].Record(commands,1);uav(s.feature);s.qkv[i].Record(g_recorder,commands);uav(s.qkv_raw);s.window[i].Record(commands,0);uav(s.qkv_float);s.window[i].Record(commands,1);uav(s.attention_float);s.window[i].Record(commands,2);uav(s.attention);s.attention_project[i].Record(g_recorder,commands);uav(s.attention_raw);s.boundary[i].Record(commands,2);uav(s.main[(i&1)^1]);}
    const auto n=++submissions;if(n==1||n%120==0)log("%s_submit=%llu ffx_frame=%llu output=%p\n",label,n,frame,s.main[0]);return true;
}

bool record_blocks9_14(ID3D12GraphicsCommandList *commands,unsigned long long frame){return record_swin_stage<RuntimeS128,6>(g_s128,g_s64_main[0],commands,frame,g_s128_ready,g_s128_submissions,"blocks9_14",&g_s256_submissions);}
bool record_blocks15_22(ID3D12GraphicsCommandList *commands,unsigned long long frame){return record_swin_stage<RuntimeS256,8>(g_s256,g_s128.main[0],commands,frame,g_s256_ready,g_s256_submissions,"blocks15_22",&g_s512_submissions);}
bool record_blocks23_30(ID3D12GraphicsCommandList *commands,unsigned long long frame){return record_swin_stage<RuntimeS512,8>(g_s512,g_s256.main[0],commands,frame,g_s512_ready,g_s512_submissions,"blocks23_30",&g_vit_submissions);}

bool record_blocks31_38(ID3D12GraphicsCommandList *commands,unsigned long long frame){
    if(!commands||!g_vit_ready.load())return false;
    auto uav=[&](ID3D12Resource*r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;commands->ResourceBarrier(1,&b);};
    auto transition=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES z){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=a;b.Transition.StateAfter=z;commands->ResourceBarrier(1,&b);};
    if(g_vit_submissions.load()){if(g_d512_submissions.load())transition(g_vit.source[0],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_vit.cropped,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_vit.predown_mid,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    transition(g_s512.main[0],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g_vit.crop.Record(commands);transition(g_vit.cropped,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g_vit.predown.Record(commands,0);transition(g_vit.predown_mid,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g_vit.predown.Record(commands,1);uav(g_vit.source[0]);
    for(UINT i=0;i<8;i++){UINT p=i&1;g_vit.front[p].Record(commands,0);uav(g_vit.main);g_vit.expand[i].Record(g_recorder,commands);uav(g_vit.branch);g_vit.front[p].Record(commands,1);uav(g_vit.contract_input);g_vit.contract[i].Record(g_recorder,commands);uav(g_vit.contract_raw);g_vit.front[p].Record(commands,2);uav(g_vit.hidden);g_vit.qkv[i].Record(g_recorder,commands);uav(g_vit.qkv_out);g_vit.pack[i].Record(commands);uav(g_vit.q);uav(g_vit.k);uav(g_vit.v);g_vit.qk[i].Record(g_recorder,commands);uav(g_vit.score);g_vit.softmax.Record(commands);uav(g_vit.prob);g_vit.av[i].Record(g_recorder,commands);uav(g_vit.attention);g_vit.output[p].Record(commands,0);uav(g_vit.attention_input);g_vit.projection[i].Record(g_recorder,commands);uav(g_vit.projection_raw);g_vit.output[p].Record(commands,1);uav(g_vit.source[p^1]);}
    const auto n=++g_vit_submissions;if(n==1||n%120==0)log("blocks31_38_submit=%llu ffx_frame=%llu output=%p tokens=540\n",n,frame,g_vit.source[0]);return true;
}

bool record_blocks39_47(ID3D12GraphicsCommandList*commands,unsigned long long frame){
    if(!commands||!g_d512_ready.load())return false;auto uav=[&](ID3D12Resource*r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;commands->ResourceBarrier(1,&b);};auto transition=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES z){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=a;b.Transition.StateAfter=z;commands->ResourceBarrier(1,&b);};
    if(g_d256_submissions.load())transition(g_d512.main[0],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(g_vit.source[0],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g_d512.prefix_pass.Record(commands,0);uav(g_d512.combined);g_d512.prefix.Record(g_recorder,commands);uav(g_d512.prefix_raw);g_d512.prefix_pass.Record(commands,1);uav(g_d512.main[0]);
    for(UINT i=0;i<8;i++){g_d512.gate[i].Record(g_recorder,commands);uav(g_d512.gate_out);g_d512.up[i].Record(g_recorder,commands);uav(g_d512.up_out);g_d512.boundary[i].Record(commands,0);uav(g_d512.hidden);g_d512.project[i].Record(g_recorder,commands);uav(g_d512.project_raw);g_d512.boundary[i].Record(commands,1);uav(g_d512.feature);g_d512.qkv[i].Record(g_recorder,commands);uav(g_d512.qkv_raw);g_d512.window[i].Record(commands,0);uav(g_d512.qkv_float);g_d512.window[i].Record(commands,1);uav(g_d512.attention_float);g_d512.window[i].Record(commands,2);uav(g_d512.attention);g_d512.attention_project[i].Record(g_recorder,commands);uav(g_d512.attention_raw);g_d512.boundary[i].Record(commands,2);uav(g_d512.main[(i&1)^1]);}
    const auto n=++g_d512_submissions;if(n==1||n%120==0)log("blocks39_47_submit=%llu ffx_frame=%llu output=%p\n",n,frame,g_d512.main[0]);return true;
}

bool record_blocks48_55(ID3D12GraphicsCommandList*commands,unsigned long long frame){
    if(!commands||!g_d256_ready.load())return false;auto uav=[&](ID3D12Resource*r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;commands->ResourceBarrier(1,&b);};auto transition=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES z){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=a;b.Transition.StateAfter=z;commands->ResourceBarrier(1,&b);};if(g_d128_submissions.load())transition(g_d256.main[0],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_d512.main[0],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g_d256.prefix_pass.Record(commands,0);uav(g_d256.packed);g_d256.prefix.Record(g_recorder,commands);uav(g_d256.prefix_raw);g_d256.prefix_pass.Record(commands,1);uav(g_d256.main[0]);for(UINT i=0;i<8;i++){g_d256.gate[i].Record(g_recorder,commands);uav(g_d256.gate_out);g_d256.boundary[i].Record(commands,0);uav(g_d256.hidden);g_d256.project[i].Record(g_recorder,commands);uav(g_d256.project_raw);g_d256.boundary[i].Record(commands,1);uav(g_d256.feature);g_d256.qkv[i].Record(g_recorder,commands);uav(g_d256.qkv_raw);g_d256.window[i].Record(commands,0);uav(g_d256.qkv_float);g_d256.window[i].Record(commands,1);uav(g_d256.attention_float);g_d256.window[i].Record(commands,2);uav(g_d256.attention);g_d256.attention_project[i].Record(g_recorder,commands);uav(g_d256.attention_raw);g_d256.boundary[i].Record(commands,2);uav(g_d256.main[(i&1)^1]);}const auto n=++g_d256_submissions;if(n==1||n%120==0)log("blocks48_55_submit=%llu ffx_frame=%llu output=%p\n",n,frame,g_d256.main[0]);return true;
}

bool record_blocks56_61(ID3D12GraphicsCommandList*commands,unsigned long long frame){if(!commands||!g_d128_ready.load())return false;auto uav=[&](ID3D12Resource*r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;commands->ResourceBarrier(1,&b);};auto transition=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES z){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=a;b.Transition.StateAfter=z;commands->ResourceBarrier(1,&b);};if(g_d64_submissions.load())transition(g_d128.main[0],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_d256.main[0],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g_d128.prefix_pass.Record(commands,0);uav(g_d128.packed);g_d128.prefix.Record(g_recorder,commands);uav(g_d128.prefix_raw);g_d128.prefix_pass.Record(commands,1);uav(g_d128.main[0]);for(UINT i=0;i<6;i++){g_d128.gate[i].Record(g_recorder,commands);uav(g_d128.gate_out);g_d128.boundary[i].Record(commands,0);uav(g_d128.hidden);g_d128.project[i].Record(g_recorder,commands);uav(g_d128.project_raw);g_d128.boundary[i].Record(commands,1);uav(g_d128.feature);g_d128.qkv[i].Record(g_recorder,commands);uav(g_d128.qkv_raw);g_d128.window[i].Record(commands,0);uav(g_d128.qkv_float);g_d128.window[i].Record(commands,1);uav(g_d128.attention_float);g_d128.window[i].Record(commands,2);uav(g_d128.attention);g_d128.attention_project[i].Record(g_recorder,commands);uav(g_d128.attention_raw);g_d128.boundary[i].Record(commands,2);uav(g_d128.main[(i&1)^1]);}const auto n=++g_d128_submissions;if(n==1||n%120==0)log("blocks56_61_submit=%llu ffx_frame=%llu output=%p\n",n,frame,g_d128.main[0]);return true;}

bool record_blocks62_65(ID3D12GraphicsCommandList*commands,unsigned long long frame){if(!commands||!g_d64_ready.load())return false;auto uav=[&](ID3D12Resource*r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;commands->ResourceBarrier(1,&b);};auto transition=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES z){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=a;b.Transition.StateAfter=z;commands->ResourceBarrier(1,&b);};if(g_d32_submissions.load())transition(g_d64.main[0],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_d128.main[0],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g_d64.prefix_pass.Record(commands,0);uav(g_d64.packed);g_d64.prefix.Record(g_recorder,commands);uav(g_d64.prefix_raw);g_d64.prefix_pass.Record(commands,1);uav(g_d64.main[0]);for(UINT i=0;i<4;i++){g_d64.gate[i].Record(g_recorder,commands);uav(g_d64.gate_out);g_d64.boundary[i].Record(commands,0);uav(g_d64.hidden);g_d64.project[i].Record(g_recorder,commands);uav(g_d64.project_raw);g_d64.boundary[i].Record(commands,1);uav(g_d64.feature);g_d64.qkv[i].Record(g_recorder,commands);uav(g_d64.qkv_raw);g_d64.window[i].Record(commands,0);uav(g_d64.qkv_float);g_d64.window[i].Record(commands,1);uav(g_d64.attention_float);g_d64.window[i].Record(commands,2);uav(g_d64.attention);g_d64.attention_project[i].Record(g_recorder,commands);uav(g_d64.attention_raw);g_d64.boundary[i].Record(commands,2);uav(g_d64.main[(i&1)^1]);}const auto n=++g_d64_submissions;if(n==1||n%120==0)log("blocks62_65_submit=%llu ffx_frame=%llu output=%p\n",n,frame,g_d64.main[0]);return true;}

bool record_blocks66_69(ID3D12GraphicsCommandList*commands,unsigned long long frame){if(!commands||!g_d32_ready.load())return false;auto transition=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES z){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=a;b.Transition.StateAfter=z;commands->ResourceBarrier(1,&b);};auto uav=[&](ID3D12Resource*r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;commands->ResourceBarrier(1,&b);};if(g_d32_submissions.load()){transition(g_d32.half,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_d32.prefix_fp32,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_d32.feature,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_d32.qkv,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_d32.main[0],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_d32.main[1],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}transition(g_d64.main[0],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g_d32.prefix_pass.Record(commands,0);uav(g_d32.packed);g_d32.prefix.Record(g_recorder,commands);uav(g_d32.raw);g_d32.prefix_pass.Record(commands,1);transition(g_d32.half,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g_d32.bridge.Record(commands);transition(g_d32.prefix_fp32,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);ID3D12DescriptorHeap*h[]={g_d32.heap};commands->SetDescriptorHeaps(1,h);commands->SetComputeRootSignature(g_d32.root);UINT step=g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);const UINT groups=(522240+63)/64;for(UINT b=0;b<4;b++){if(b){transition(g_d32.feature,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_d32.qkv,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);if(b>=2)transition(g_d32.main[b&1],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}auto table=g_d32.heap->GetGPUDescriptorHandleForHeapStart();table.ptr+=UINT64(b)*7*step;commands->SetComputeRootDescriptorTable(0,table);commands->SetPipelineState(g_d32.pso[b][0]);commands->Dispatch(groups,1,1);transition(g_d32.feature,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);commands->SetPipelineState(g_d32.pso[b][1]);commands->Dispatch(groups,1,1);transition(g_d32.qkv,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);commands->SetPipelineState(g_d32.pso[b][2]);commands->Dispatch(groups,1,1);transition(g_d32.main[b&1],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}const auto n=++g_d32_submissions;if(n==1||n%120==0)log("blocks66_69_submit=%llu ffx_frame=%llu output=%p\n",n,frame,g_d32.main[1]);return true;}

bool record_block70(ID3D12GraphicsCommandList*commands,ID3D12Resource*output,unsigned long long frame){if(!commands||!output||!g_b70_ready.load())return false;auto transition=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES z){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=a;b.Transition.StateAfter=z;commands->ResourceBarrier(1,&b);};auto uav=[&](ID3D12Resource*r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;commands->ResourceBarrier(1,&b);};if(g_b70_submissions.load()){transition(g_b70.prefix,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_b70.feature,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_b70.qkv,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(g_b70.body,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}auto dispatch=[&](UINT64 threads){UINT64 groups=(threads+63)/64;commands->Dispatch((UINT)std::min<UINT64>(groups,65535),(UINT)((groups+65534)/65535),1);};ID3D12DescriptorHeap*h[]={g_b70.prefix_heap};commands->SetDescriptorHeaps(1,h);commands->SetComputeRootSignature(g_b70.prefix_root);commands->SetComputeRootDescriptorTable(0,g_b70.prefix_heap->GetGPUDescriptorHandleForHeapStart());commands->SetPipelineState(g_b70.prefix_pso);dispatch(32640ull*2048);if(g_gpu_profile.state.load()==1)g_gpu_profile.Mark(commands,14);transition(g_b70.prefix,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);h[0]=g_b70.body_heap;commands->SetDescriptorHeaps(1,h);commands->SetComputeRootSignature(g_b70.body_root);commands->SetComputeRootDescriptorTable(0,g_b70.body_heap->GetGPUDescriptorHandleForHeapStart());commands->SetPipelineState(g_b70.body_pso[0]);dispatch(1920ull*1088);if(g_gpu_profile.state.load()==1)g_gpu_profile.Mark(commands,15);transition(g_b70.feature,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);commands->SetPipelineState(g_b70.body_pso[1]);dispatch(1920ull*1088);if(g_gpu_profile.state.load()==1)g_gpu_profile.Mark(commands,16);transition(g_b70.qkv,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);commands->SetPipelineState(g_b70.body_pso[2]);dispatch(1920ull*1088);if(g_gpu_profile.state.load()==1)g_gpu_profile.Mark(commands,17);transition(g_b70.body,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if(g_b70_packed_copied.exchange(false))transition(g_b70.packed,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const bool key_down=(GetAsyncKeyState(VK_F6)&0x8000)!=0;if(key_down&&!g_output_key_down){g_output_mode=(g_output_mode+1)%3;log("output_mode=%u (0=neural 1=base 2=left_base_right_neural)\n",g_output_mode);}g_output_key_down=key_down;const auto n=++g_b70_submissions;if(n==1||n%120==0)log("block70_submit=%llu ffx_frame=%llu output=display_residual\n",n,frame);return true;}

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
    const ULONGLONG begin = GetTickCount64();wchar_t value[16]{};if(GetEnvironmentVariableW(L"DLSS5_MAX_BLOCK",value,16))g_max_block=(UINT)_wtoi(value);else if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\runtime-max-block.txt)",L"rb")){unsigned v=70;if(fscanf(f,"%u",&v)==1)g_max_block=v;fclose(f);}g_present_enabled=false;if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\runtime-disable-present.txt)",L"rb")){unsigned v=1;if(fscanf(f,"%u",&v)==1)g_present_enabled=v==0;fclose(f);}log("runtime_gate max_block=%u raw_r10_present=%u\n",g_max_block,g_present_enabled?1u:0u);
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
            initialize_blocks23_30();
            initialize_blocks31_38();
            initialize_blocks39_47();
            initialize_blocks48_55();
            initialize_blocks56_61();
            initialize_blocks62_65();
            initialize_blocks66_69();
            initialize_block70();
            g_gpu_profile.Create(g_device);
            // Optional measured candidate; the existing FP32 path remains available.
            if(GetFileAttributesW(LR"(D:\DLSSNR-Lab\enable-block70-sm6.txt)")!=INVALID_FILE_ATTRIBUTES){
                ID3DBlob *code=nullptr;dmlrt_check("block70 sm6 read",D3DReadFileToBlob(LR"(D:\DLSSNR-Lab\shader-cache\block70-1920x1088-ffn.cso)",&code));
                D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=g_b70.body_root;p.CS={code->GetBufferPointer(),code->GetBufferSize()};ID3D12PipelineState *candidate=nullptr;dmlrt_check("block70 sm6 pso",g_device->CreateComputePipelineState(&p,IID_PPV_ARGS(&candidate)));g_b70.body_pso[0]->Release();g_b70.body_pso[0]=candidate;code->Release();log("block70_ffn=sm6_fp32\n");
            }
            D3D12_RESOURCE_DESC display_desc{};display_desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;display_desc.Width=1920;display_desc.Height=1080;display_desc.DepthOrArraySize=display_desc.MipLevels=1;display_desc.Format=DXGI_FORMAT_R10G10B10A2_UNORM;display_desc.SampleDesc.Count=1;display_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            D3D12_HEAP_PROPERTIES display_heap{};display_heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            dmlrt_check("display resource",g_device->CreateCommittedResource(&display_heap,D3D12_HEAP_FLAG_NONE,&display_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&g_display_texture)));
            auto display_cpu=g_b70.output_heap[0]->GetCPUDescriptorHandleForHeapStart();display_cpu.ptr+=2ull*g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);D3D12_UNORDERED_ACCESS_VIEW_DESC display_uav{};display_uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;display_uav.Format=display_desc.Format;g_device->CreateUnorderedAccessView(g_display_texture,nullptr,&display_uav,display_cpu);
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
    if (!resize) return;
    auto *native = reinterpret_cast<IDXGISwapChain3 *>(
        static_cast<uintptr_t>(swapchain->get_native()));
    ID3D12Device *device = nullptr;
    const HRESULT hr = native ? native->GetDevice(IID_PPV_ARGS(&device)) : E_POINTER;
    if (FAILED(hr)) {
        log("resident_swapchain_device_failed swapchain=%p hr=0x%08x\n",
            native, static_cast<unsigned>(hr));
        return;
    }
    if(!g_main_swapchain){g_main_swapchain=native;g_main_swapchain->AddRef();}
    if(!g_device){g_device=device;device=nullptr;}
    DXGI_SWAP_CHAIN_DESC desc{};
    native->GetDesc(&desc);
    log("resident_start swapchain=%p device=%p size=%ux%u format=%u\n",
        native, g_device, desc.BufferDesc.Width, desc.BufferDesc.Height,
        static_cast<unsigned>(desc.BufferDesc.Format));
    if(device)device->Release();
}

void on_present(reshade::api::command_queue *queue, reshade::api::swapchain *swapchain,
                const reshade::api::rect *, const reshade::api::rect *, uint32_t,
                const reshade::api::rect *) {
    const auto n = ++g_presents;
    auto*native=reinterpret_cast<IDXGISwapChain3*>(static_cast<uintptr_t>(swapchain->get_native()));
    if(native==g_main_swapchain&&(n==1||n%600==0)){const UINT index=native->GetCurrentBackBufferIndex();ID3D12Resource*b=nullptr;if(SUCCEEDED(native->GetBuffer(index,IID_PPV_ARGS(&b)))){const auto d=b->GetDesc();log("present_backbuffer=%llu index=%u size=%llux%u format=%u\n",n,index,(unsigned long long)d.Width,d.Height,(UINT)d.Format);b->Release();}}
    const auto generation=g_b70_submissions.load();
    if(native==g_main_swapchain&&g_gpu_profile.state.load()==2){
        auto *native_queue=reinterpret_cast<ID3D12CommandQueue*>(queue->get_native());
        if(SUCCEEDED(native_queue->GetTimestampFrequency(&g_gpu_profile.frequency))&&SUCCEEDED(native_queue->Signal(g_gpu_profile.fence,1)))g_gpu_profile.state.store(3);
    }else if(native==g_main_swapchain&&g_gpu_profile.state.load()==3&&g_gpu_profile.fence->GetCompletedValue()==1){
        UINT64 *times=nullptr;D3D12_RANGE range{0,18*sizeof(UINT64)};
        if(SUCCEEDED(g_gpu_profile.readback->Map(0,&range,reinterpret_cast<void**>(&times)))){
            const char *names[]={"block0","front1_4","encoder5_8","encoder9_14","encoder15_22","encoder23_30","vit31_38","decoder39_47","decoder48_55","decoder56_61","decoder62_65","decoder66_69","block70"};
            for(UINT i=0;i<13;i++)log("gpu_profile %s_ms=%.6f\n",names[i],1000.0*double(times[i+1]-times[i])/g_gpu_profile.frequency);
            log("gpu_profile network_ms=%.6f\n",1000.0*double(times[13]-times[0])/g_gpu_profile.frequency);
            log("gpu_profile block70_prefix_ms=%.6f ffn_ms=%.6f qkv_ms=%.6f attention_ms=%.6f\n",1000.0*double(times[14]-times[12])/g_gpu_profile.frequency,1000.0*double(times[15]-times[14])/g_gpu_profile.frequency,1000.0*double(times[16]-times[15])/g_gpu_profile.frequency,1000.0*double(times[17]-times[16])/g_gpu_profile.frequency);
            D3D12_RANGE none{0,0};g_gpu_profile.readback->Unmap(0,&none);
        }
        g_gpu_profile.state.store(4);
    }
    if(g_ready.load()&&generation){const auto now=GetTickCount64();if(!g_cadence_start){g_cadence_start=now;g_cadence_frames=generation;}else if(now-g_cadence_start>=5000){log("cadence frames=%llu elapsed_ms=%llu fps=%.3f\n",generation-g_cadence_frames,now-g_cadence_start,1000.0*(generation-g_cadence_frames)/(now-g_cadence_start));g_cadence_start=now;g_cadence_frames=generation;}}
    if(native==g_main_swapchain&&g_ready.load()&&generation>g_display_generation){
        ID3D12Resource *back=nullptr;
        if(SUCCEEDED(native->GetBuffer(native->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&back)))){
            const auto desc=back->GetDesc();
            if(desc.Width==1920&&desc.Height==1080&&desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM){
                auto *cmd=reinterpret_cast<ID3D12GraphicsCommandList*>(queue->get_immediate_command_list()->get_native());
                auto tr=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES z){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,z};cmd->ResourceBarrier(1,&b);};
                if(g_display_generation)tr(g_display_texture,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
                tr(back,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);cmd->CopyResource(g_display_texture,back);
                tr(g_display_texture,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                ID3D12DescriptorHeap *heaps[]={g_b70.output_heap[0]};cmd->SetDescriptorHeaps(1,heaps);cmd->SetComputeRootSignature(g_b70.output_root);cmd->SetComputeRootDescriptorTable(0,heaps[0]->GetGPUDescriptorHandleForHeapStart());cmd->SetComputeRoot32BitConstant(1,g_output_mode,0);cmd->SetPipelineState(g_b70.out_pso);cmd->Dispatch((1920*1080+63)/64,1,1);
                tr(g_display_texture,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);tr(back,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);cmd->CopyResource(back,g_display_texture);tr(back,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT);
                g_display_generation=generation;if(generation==1||generation%120==0)log("display_residual generation=%llu mode=%u\n",generation,g_output_mode);
            }
            back->Release();
        }
    }
    if(false&&g_present_enabled&&native==g_main_swapchain&&g_b70_submissions.load()&&!g_b70_packed_copied.load()){
        const UINT index=native->GetCurrentBackBufferIndex();ID3D12Resource*backbuffer=nullptr;
        if(SUCCEEDED(native->GetBuffer(index,IID_PPV_ARGS(&backbuffer)))){const auto desc=backbuffer->GetDesc();if(desc.Width==1920&&desc.Height==1080&&desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM){auto*cmd=queue->get_immediate_command_list();const reshade::api::resource src{reinterpret_cast<uint64_t>(g_b70.packed)},dst{reinterpret_cast<uint64_t>(backbuffer)};cmd->barrier(src,reshade::api::resource_usage::unordered_access,reshade::api::resource_usage::copy_source);cmd->barrier(dst,reshade::api::resource_usage::present,reshade::api::resource_usage::copy_dest);cmd->copy_buffer_to_texture(src,0,1920,1080,dst,0,nullptr);cmd->barrier(dst,reshade::api::resource_usage::copy_dest,reshade::api::resource_usage::present);g_b70_packed_copied.store(true);if(n==1||n%120==0)log("r10_backbuffer_submit=%llu index=%u resource=%p\n",n,index,backbuffer);}backbuffer->Release();}
    }
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
