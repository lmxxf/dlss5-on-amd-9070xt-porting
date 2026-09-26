#include "LmxxfNrApi.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include "LmxxfProductionOptions.h"
#include "native_device_identity.h"
#include "native_game_codec.h"
#include "native_lab_paths.h"
#include "native_game_rgb_input.h"
#include "native_network_geometry.h"
#include "native_rgb_texture.h"
#include "hip_d3d12_bridge.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace
{
thread_local char g_lastError[256] = {};

// LMXXF_NR_FRAME_INFO_V1_SIZE is what an ABI v1 host sends as struct_size. It must equal the
// offset where the exposure fields start, or an old host's frames are rejected outright.
static_assert(offsetof(LmxxfNrFrameInfo, exposure) == LMXXF_NR_FRAME_INFO_V1_SIZE,
              "LMXXF_NR_FRAME_INFO_V1_SIZE must match the ABI v1 LmxxfNrFrameInfo size");

// Bound each D3D12 queue wait during EnqueueHip recovery to limit stalls.
// Teardown keeps its 30 s wait; HIP stream synchronization is not bounded here.
constexpr DWORD kSubmissionWaitMs = 3000;

void SetError(const char *text)
{
    if (!text)
        text = "";
    std::strncpy(g_lastError, text, sizeof(g_lastError) - 1);
    g_lastError[sizeof(g_lastError) - 1] = 0;
}

int32_t Fail(int32_t status, const char *text)
{
    SetError(text);
    return status;
}

std::string Utf8(const std::wstring &s)
{
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    if (n)
        WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}

bool IsDirectory(const std::wstring &path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool FileExists(const std::wstring &path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring JoinPath(const std::wstring &dir, const wchar_t *name)
{
    std::wstring out = dir;
    if (!out.empty() && out.back() != L'\\' && out.back() != L'/')
        out += L'\\';
    out += name;
    return out;
}

std::wstring DllDirectory()
{
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&LmxxfNrGetApi), &mod);
    wchar_t path[MAX_PATH] {};
    if (!mod || !GetModuleFileNameW(mod, path, MAX_PATH))
        return {};
    std::wstring dir(path);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        dir.resize(slash);
    return dir;
}

std::wstring FindShaderDir(const std::wstring &assets = {})
{
    const std::wstring dll = DllDirectory();
    const std::wstring candidates[] = {
        JoinPath(assets, L"shaders"),
        JoinPath(dll, L"shaders"),
        L"shaders",
        L"third_party\\lmxxf\\shaders", // dev fallback
    };
    for (const auto &c : candidates)
    {
        if (c.empty())
            continue;
        wchar_t full[MAX_PATH] {};
        GetFullPathNameW(c.c_str(), MAX_PATH, full, nullptr);
        if (FileExists(JoinPath(full, L"native_codec_encode.hlsl")))
            return full;
    }
    return {};
}

std::wstring FindWeightsDir(const std::wstring &assets)
{
    if (FileExists(JoinPath(assets, L"block0-ffn.f16")) || FileExists(JoinPath(assets, L"block0-ffn.f32")))
        return assets;
    const std::wstring sub = JoinPath(assets, L"weights");
    if (FileExists(JoinPath(sub, L"block0-ffn.f16")) || FileExists(JoinPath(sub, L"block0-ffn.f32")))
        return sub;
    wchar_t env[MAX_PATH] {};
    if (GetEnvironmentVariableW(L"LMXXF_WEIGHTS_DIR", env, MAX_PATH) && env[0])
    {
        wchar_t full[MAX_PATH] {};
        GetFullPathNameW(env, MAX_PATH, full, nullptr);
        if (FileExists(JoinPath(full, L"block0-ffn.f16")) || FileExists(JoinPath(full, L"block0-ffn.f32")))
            return full;
    }
    return {};
}

bool ResolveModulesDir(const std::wstring &assets, std::wstring *modulesDir)
{
    if (FileExists(JoinPath(assets, L"SHA256SUMS")))
    {
        *modulesDir = assets;
        return true;
    }
    const std::wstring hip = JoinPath(assets, L"HIP");
    if (FileExists(JoinPath(hip, L"SHA256SUMS")))
    {
        *modulesDir = hip;
        return true;
    }
    const std::wstring modules = JoinPath(assets, L"modules");
    if (FileExists(JoinPath(modules, L"SHA256SUMS")))
    {
        *modulesDir = modules;
        return true;
    }
    const std::wstring nested = JoinPath(JoinPath(assets, L"native-game-tiled-assets"), L"HIP");
    if (FileExists(JoinPath(nested, L"SHA256SUMS")))
    {
        *modulesDir = nested;
        return true;
    }
    return false;
}

int32_t ValidateModuleSet(const std::wstring &modulesDir, uint32_t *outCount)
{
    *outCount = 0;
    const std::wstring sumsPath = JoinPath(modulesDir, L"SHA256SUMS");
    HANDLE file = CreateFileW(sumsPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return Fail(LMXXF_NR_UNAVAILABLE, "Create: SHA256SUMS missing in modules directory");
    LARGE_INTEGER size {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1 << 20)
    {
        CloseHandle(file);
        return Fail(LMXXF_NR_UNAVAILABLE, "Create: SHA256SUMS unreadable");
    }
    std::string text(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr))
    {
        CloseHandle(file);
        return Fail(LMXXF_NR_UNAVAILABLE, "Create: SHA256SUMS read failed");
    }
    CloseHandle(file);
    text.resize(read);

    uint32_t found = 0;
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
            eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        const size_t sp = line.find_first_of(" \t");
        if (sp == std::string::npos)
            continue;
        size_t nameStart = line.find_first_not_of(" \t", sp);
        if (nameStart == std::string::npos)
            continue;
        std::string name = line.substr(nameStart);
        if (name.size() < 7 || name.rfind(".hsaco") != name.size() - 6)
            continue;
        std::wstring wname(name.begin(), name.end());
        const std::wstring full = JoinPath(modulesDir, wname.c_str());
        if (!IsDirectory(full) && GetFileAttributesW(full.c_str()) != INVALID_FILE_ATTRIBUTES)
            ++found;
        else
            return Fail(LMXXF_NR_UNAVAILABLE, "Create: hsaco listed in SHA256SUMS is missing");
    }
    if (found == 0)
        return Fail(LMXXF_NR_UNAVAILABLE, "Create: no .hsaco entries in SHA256SUMS");
    if (found < 24)
        return Fail(LMXXF_NR_UNAVAILABLE, "Create: fewer than 24 hsaco modules; host/module set incomplete");
    *outCount = found;
    return static_cast<int32_t>(LMXXF_NR_OK);
}

bool LooksLikeObject(void *p)
{
    if (!p)
        return false;
    MEMORY_BASIC_INFORMATION info {};
    if (!VirtualQuery(p, &info, sizeof info))
        return false;
    if (!(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
        return false;
    return info.State == MEM_COMMIT;
}

struct Job
{
    uint32_t state = LMXXF_NR_JOB_NONE;
    ID3D12Resource *color = nullptr;
    D3D12_RESOURCE_STATES colorState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    UINT width = 0, height = 0;
    uint32_t seed = 1;
    float transfer_strength = 1.0f;
    float color_strength = 1.0f;
    uint32_t debug_view = 0;
    float pre_exposure = 1.0f;
    float exposure_scale = 1.0f;
    /* The game's exposure texture and its state at RecordInputs: the source of the per-frame
     * copy into Session::exposureCopy. Not bound to any codec. */
    ID3D12Resource *sourceExposure = nullptr;
    D3D12_RESOURCE_STATES sourceExposureState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    bool codec_passthrough = false;
};

struct Session
{
    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    ID3D12CommandQueue *fallbackConsumerQueue = nullptr;
    std::wstring assetsDir;
    std::wstring modulesDir;
    std::wstring weightsDir;
    std::wstring shaderDir;
    uint32_t hsacoCount = 0;
    bool modulesValidated = false;
    bool hipPrepared = false;
    bool queueBound = false;
    bool zeroOutputFallback = false;
    bool failed = false; /* Fail-closed poisoning */
    hip_reference::D3D12Bridge *bridge = nullptr;
    NativeGameCodec *encode = nullptr;
    NativeGameRgbInput *rgbInput = nullptr;
    NativeRgbTexture *rgbTex = nullptr;
    NativeGameCodec *decode = nullptr;
    ID3D12Resource *decodeDisplay = nullptr;
    Job job {};
    DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN;
    /* The codecs bind OUR stable 1x1 copy, never the game's texture. An engine may hand us a
     * new allocation every frame, and the codec bakes the exposure SRV at Create, so binding the
     * game's pointer would rebuild the whole chain (including a warm-up dispatch) every frame. A
     * copy also pins the format - CopyTextureRegion needs matching formats - so only a change of
     * the SOURCE's format recreates it and therefore rebuilds. */
    ID3D12Resource *exposureCopy = nullptr;
    DXGI_FORMAT exposureCopyFormat = DXGI_FORMAT_UNKNOWN;
    /* What the live codecs were actually created with (exposureCopy, or null when running
     * without exposure). */
    ID3D12Resource *boundExposure = nullptr;
    /* The Color texture's ALLOCATION size when the codecs were created. Deliberately separate
     * from job.width/height, which holds the NGX subrect: the two are not interchangeable, and
     * comparing one against the other rebuilds the chain every frame of any title whose render
     * subrect is smaller than its buffer (UE5 at a non-native DLSS scale). */
    UINT allocWidth = 0, allocHeight = 0;
    /* Count of codec+HIP teardowns triggered by geoChanged (exposure/valid/format/alloc). */
    uint32_t codecRecreates = 0;

    // NativeGameCodec::Record wants one state per source plus one more for the exposure SRV.
    // RecordInputs always leaves the copy in NON_PIXEL_SHADER_RESOURCE.
    std::vector<D3D12_RESOURCE_STATES> CodecStates(std::vector<D3D12_RESOURCE_STATES> s) const
    {
        if (boundExposure)
            s.push_back(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return s;
    }


    void TeardownCodecChain()
    {
        if (bridge)
        {
            try
            {
                bridge->CancelUnsubmitted();
                bridge->NotifyOutputSubmittedIfRecorded(queue);
            }
            catch (...)
            {
                AbandonSessionResources();
                throw std::runtime_error("TeardownCodecChain: bridge acknowledgement failed");
            }
            if (!bridge->WaitForSubmittedWork())
            {
                AbandonSessionResources();
                throw std::runtime_error("TeardownCodecChain: bridge work did not complete");
            }
        }
        delete bridge;
        bridge = nullptr;
        hipPrepared = false;
        if (decodeDisplay)
        {
            decodeDisplay->Release();
            decodeDisplay = nullptr;
        }
        delete decode;
        decode = nullptr;
        delete rgbTex;
        rgbTex = nullptr;
        delete rgbInput;
        rgbInput = nullptr;
        delete encode;
        encode = nullptr;
        colorFormat = DXGI_FORMAT_UNKNOWN;
    }

    // Wait until queue work that may touch encode/rgb/bridge shared resources is done.
    // Returns S_OK only when completion is confirmed; callers must retain resources on failure.
    HRESULT DrainQueue(ID3D12CommandQueue *target, DWORD timeoutMs = 30000)
    {
        if (!device || !target)
            return S_OK;
        if (FAILED(device->GetDeviceRemovedReason()))
            return DXGI_ERROR_DEVICE_REMOVED;
        ID3D12Fence *fence = nullptr;
        HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        if (FAILED(hr) || !fence)
            return FAILED(hr) ? hr : E_FAIL;
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!ev)
        {
            const DWORD err = GetLastError();
            fence->Release();
            return HRESULT_FROM_WIN32(err ? err : ERROR_OUTOFMEMORY);
        }
        const UINT64 v = 1;
        hr = target->Signal(fence, v);
        if (FAILED(hr))
        {
            CloseHandle(ev);
            fence->Release();
            return hr;
        }
        hr = fence->SetEventOnCompletion(v, ev);
        if (FAILED(hr))
        {
            CloseHandle(ev);
            fence->Release();
            return hr;
        }
        const DWORD wr = WaitForSingleObject(ev, timeoutMs);
        const UINT64 completed = fence->GetCompletedValue();
        // A timeout can leave SetEventOnCompletion armed. Retain the event and
        // fence until process exit instead of closing a future signal target.
        if (wr == WAIT_OBJECT_0 && completed >= v)
        {
            CloseHandle(ev);
            fence->Release();
        }
        if (wr != WAIT_OBJECT_0 || completed < v)
            return wr == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : E_FAIL;
        return S_OK;
    }

    HRESULT DrainGpu(DWORD timeoutMs = 30000)
    {
        const HRESULT sessionHr = DrainQueue(queue, timeoutMs);
        if (FAILED(sessionHr))
            return sessionHr;
        if (!fallbackConsumerQueue)
            return S_OK;
        const HRESULT consumerHr = DrainQueue(fallbackConsumerQueue, timeoutMs);
        if (SUCCEEDED(consumerHr))
        {
            fallbackConsumerQueue->Release();
            fallbackConsumerQueue = nullptr;
        }
        return consumerHr;
    }

    void AbandonSessionResources()
    {
        // Fail-closed intentional leak: GPU may still reference the whole chain.
        bridge = nullptr;
        decodeDisplay = nullptr;
        decode = nullptr;
        rgbTex = nullptr;
        rgbInput = nullptr;
        encode = nullptr;
        boundExposure = nullptr;
        // Owned here rather than by a codec, but the same fail-closed rule applies: do not free
        // what the GPU may still reference.
        exposureCopy = nullptr;
        // The queue may still own GPU work. Keep our reference on fail-closed teardown.
        fallbackConsumerQueue = nullptr;
        if (queue)
        {
            queue->Release();
            queue = nullptr;
        }
        if (device)
        {
            device->Release();
            device = nullptr;
        }
    }

    ~Session()
    {
        // Fail-closed safe teardown:
        // If already poisoned or GPU drain fails or device lost, intentionally leak rather
        // than freeing memory still touched by GPU (prevents hard crash/BSOD).
        if (failed || FAILED(DrainGpu()))
        {
            AbandonSessionResources();
            return;
        }
        if (bridge)
        {
            try
            {
                bridge->CancelUnsubmitted();
                bridge->NotifyOutputSubmittedIfRecorded(queue);
            }
            catch (...)
            {
                AbandonSessionResources();
                return;
            }
            if (!bridge->WaitForSubmittedWork())
            {
                AbandonSessionResources();
                return;
            }
        }
        // Bridge dtor also synchronizes HIP / pending fence, then frees shared buffers.
        delete bridge;
        bridge = nullptr;
        if (decodeDisplay)
        {
            decodeDisplay->Release();
            decodeDisplay = nullptr;
        }
        delete decode;
        decode = nullptr;
        delete rgbTex;
        rgbTex = nullptr;
        delete rgbInput;
        rgbInput = nullptr;
        delete encode;
        encode = nullptr;
        if (exposureCopy)
            exposureCopy->Release();
        exposureCopy = nullptr;
        if (queue)
            queue->Release();
        queue = nullptr;
        if (device)
            device->Release();
        device = nullptr;
    }
};

// The codec's colour-input contract, checked here instead of letting the codec throw.
// A game that simply uses a texture we cannot represent is a per-title limit, not a GPU
// fault: it must not poison the session (which silently disables NR for the rest of the
// process). The reason names the property; "codec unverified input format/geometry" alone
// does not say which one failed.
const char *ColorInputProblem(const D3D12_RESOURCE_DESC &desc)
{
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        return "not TEXTURE2D";
    if (!NativeInputGeometry::Supported(desc.Width, desc.Height, NativeFitLargeInput()))
        return "outside admitted geometry";
    if (desc.DepthOrArraySize != 1)
        return "DepthOrArraySize != 1";
    if (desc.MipLevels != 1)
        return "MipLevels != 1";
    if (desc.SampleDesc.Count != 1)
        return "SampleDesc.Count != 1";
    if (!(NativeIsGameColor(desc.Format) || desc.Format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP))
        return "unsupported DXGI format";
    if (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)
        return "DENY_SHADER_RESOURCE";
    return nullptr;
}

void RequireSession(Session *s)
{
    if (!s)
        throw std::runtime_error("session is null");
    if (s->failed)
        throw std::runtime_error("session is poisoned due to previous fatal error");
}

void ListContract(Session *s, ID3D12GraphicsCommandList *c)
{
    if (!c)
        throw std::runtime_error("command list is null");
    if (s->queue && c->GetType() != s->queue->GetDesc().Type)
        throw std::runtime_error("command list type mismatch with session queue");
    ID3D12Device *owner = nullptr;
    HRESULT hr = c->GetDevice(IID_PPV_ARGS(&owner));
    if (FAILED(hr) || !owner)
        throw std::runtime_error("failed to query command list device");
    bool same = NativeSameDevice(owner, s->device);
    owner->Release();
    if (!same)
        throw std::runtime_error("command list device mismatch with session device");
}

void QueueContract(Session *s, ID3D12CommandQueue *q)
{
    if (!q)
        throw std::runtime_error("command queue is null");
    if (s->device)
    {
        ID3D12Device *owner = nullptr;
        HRESULT hr = q->GetDevice(IID_PPV_ARGS(&owner));
        if (FAILED(hr) || !owner)
            throw std::runtime_error("failed to query command queue device");
        bool same = NativeSameDevice(owner, s->device);
        owner->Release();
        if (!same)
            throw std::runtime_error("command queue device mismatch with session device");
    }
    if (s->queue && !NativeSameDevice(q, s->queue))
        throw std::runtime_error("command queue does not match session queue");
}

template <class Fn>
int32_t Guard(Fn &&fn)
{
    try
    {
        return fn();
    }
    catch (const std::exception &ex)
    {
        return Fail(LMXXF_NR_FAILED, ex.what());
    }
    catch (...)
    {
        return Fail(LMXXF_NR_FAILED, "unhandled exception");
    }
}

template <class Fn>
int32_t GuardSession(Session *s, Fn &&fn)
{
    if (s && s->failed)
        return Fail(LMXXF_NR_UNAVAILABLE, "session is poisoned due to previous fatal error");
    try
    {
        return fn();
    }
    catch (const std::exception &ex)
    {
        if (s)
            s->failed = true;
        return Fail(LMXXF_NR_FAILED, ex.what());
    }
    catch (...)
    {
        if (s)
            s->failed = true;
        return Fail(LMXXF_NR_FAILED, "unhandled exception; session poisoned");
    }
}

int32_t QueryCapabilities(LmxxfNrCapabilities *out)
{
    return Guard([&] {
        if (!out)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "QueryCapabilities: null out");
        if (out->struct_size != sizeof(LmxxfNrCapabilities))
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "QueryCapabilities: struct_size mismatch");
        out->abi_version = LMXXF_NR_ABI_VERSION;
        out->max_input_width = 1920;
        out->max_input_height = 1080;
        out->history_supported = 0;
        out->overlap_supported = 0;
        out->graph_supported = 0;
        out->hip_ready = 0;
        out->gfx1201_target = 1;
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}

int32_t Create(const LmxxfNrCreateInfo *info, void **context)
{
    return Guard([&] {
        if (!info || !context)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "Create: null info or context");
        *context = nullptr;
        if (info->struct_size != sizeof(LmxxfNrCreateInfo))
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "Create: struct_size mismatch");
        if (info->flags & ~LMXXF_NR_CREATE_FLAG_ZERO_OUTPUT_FALLBACK)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "Create: unknown flags");
        if (!info->device || !info->queue)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "Create: device and queue required");
        if (!info->assets_directory || !info->assets_directory[0])
            return Fail(LMXXF_NR_INVALID_ARGUMENT,
                        "Create: assets_directory required (modules dir from 68dc099 build)");

        std::wstring assets = info->assets_directory;
        if (!IsDirectory(assets))
            return Fail(LMXXF_NR_UNAVAILABLE, "Create: assets_directory is not a directory");
        std::wstring modulesDir;
        if (!ResolveModulesDir(assets, &modulesDir))
            return Fail(LMXXF_NR_UNAVAILABLE,
                        "Create: modules directory needs SHA256SUMS + hsaco (or HIP/ under assets)");
        uint32_t count = 0;
        const int32_t st = ValidateModuleSet(modulesDir, &count);
        if (st != LMXXF_NR_OK)
            return st;

        auto *session = new Session;
        session->zeroOutputFallback = (info->flags & LMXXF_NR_CREATE_FLAG_ZERO_OUTPUT_FALLBACK) != 0;
        session->assetsDir = assets;
        session->modulesDir = modulesDir;
        session->weightsDir = FindWeightsDir(assets);
        session->shaderDir = FindShaderDir(assets);
        session->hsacoCount = count;
        session->modulesValidated = true;
        if (LooksLikeObject(info->device) && LooksLikeObject(info->queue))
        {
            auto *dev = static_cast<ID3D12Device *>(info->device);
            auto *q = static_cast<ID3D12CommandQueue *>(info->queue);
            ID3D12Device *qiDev = nullptr;
            ID3D12CommandQueue *qiQ = nullptr;
            if (SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(&qiDev))) &&
                SUCCEEDED(q->QueryInterface(IID_PPV_ARGS(&qiQ))))
            {
                session->device = qiDev;
                session->queue = qiQ;
            }
            else
            {
                if (qiDev)
                    qiDev->Release();
                if (qiQ)
                    qiQ->Release();
            }
        }
        *context = session;
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}

int32_t Destroy(void *context)
{
    return Guard([&] {
        delete static_cast<Session *>(context);
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}

int32_t PrepareSession(void *context)
{
    auto *session = static_cast<Session *>(context);
    return GuardSession(session, [&] {
        if (!session)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "PrepareSession: null context");
        RequireSession(session);
        if (!session->modulesValidated)
            return Fail(LMXXF_NR_UNAVAILABLE, "PrepareSession: modules not validated");
        if (!session->device || !session->queue)
            return Fail(LMXXF_NR_INVALID_ARGUMENT,
                        "PrepareSession: device/queue are not live ID3D12 objects");
        // Upstream 0.21+: network tier follows the first real input size (DLSS5_NETWORK_HEIGHT=auto).
        // Defer HIP bridge Create until PrepareFrame so we do not bake 1920x1080 for a 720p Color.
        session->queueBound = true;
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}

int32_t PrepareFrame(void *context, const LmxxfNrFrameInfo *info, LmxxfNrJob *job)
{
    auto *session = static_cast<Session *>(context);
    return GuardSession(session, [&] {
        if (!session || !info || !job)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "PrepareFrame: null argument");
        RequireSession(session);
        // Historical sizes: 64 ends at color_state/flags, LMXXF_NR_FRAME_INFO_V1_SIZE ends at
        // model_scale. A host whose struct_size stops earlier simply has no later fields.
        const uint32_t legacySize = 64;
        const uint32_t v1Size = LMXXF_NR_FRAME_INFO_V1_SIZE;
        if ((info->struct_size != sizeof(LmxxfNrFrameInfo) && info->struct_size != v1Size &&
             info->struct_size != legacySize) ||
            job->struct_size != sizeof(LmxxfNrJob))
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "PrepareFrame: struct_size mismatch");
        job->handle = nullptr;
        job->private_output = nullptr;
        if (session->fallbackConsumerQueue && FAILED(session->DrainGpu()))
        {
            session->failed = true;
            return Fail(LMXXF_NR_FAILED, "PrepareFrame: fallback consumer queue did not drain");
        }
        if (!session->queueBound && !session->hipPrepared)
            return Fail(LMXXF_NR_NOT_IMPLEMENTED, "PrepareFrame: call PrepareSession with a live D3D12 queue first");
        if (!info->color || !info->color_width || !info->color_height)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "PrepareFrame: color resource and size required");
        if (session->bridge && session->bridge->CurrentPhase() != hip_reference::D3D12Bridge::Phase::Ready)
        {
            session->bridge->CancelUnsubmitted();
            if (session->bridge->CurrentPhase() == hip_reference::D3D12Bridge::Phase::OutputRecorded)
            {
                session->bridge->NotifyOutputSubmitted(session->queue);
                session->DrainGpu();
            }
            if (session->bridge->CurrentPhase() != hip_reference::D3D12Bridge::Phase::Ready)
                return Fail(LMXXF_NR_UNAVAILABLE, "PrepareFrame: previous frame consumer not yet submitted (bridge not Ready)");
        }
        const uint32_t allowedFlags = LMXXF_NR_FRAME_FLAG_STRENGTH | LMXXF_NR_FRAME_FLAG_DEBUG_VIEW | LMXXF_NR_FRAME_FLAG_CODEC_PASSTHROUGH;
        if ((info->flags & ~allowedFlags) != 0)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "PrepareFrame: unknown flags");
        if (session->shaderDir.empty())
            session->shaderDir = FindShaderDir(session->assetsDir);
        if (session->shaderDir.empty())
            return Fail(LMXXF_NR_UNAVAILABLE, "PrepareFrame: native_codec_encode.hlsl not found");

        float transfer_strength = 1.0f;
        float color_strength = 1.0f;
        uint32_t debug_view = 0;
        float model_scale = 1.0f;
        if (info->struct_size >= sizeof(LmxxfNrFrameInfo))
        {
            if (info->flags & LMXXF_NR_FRAME_FLAG_STRENGTH)
            {
                transfer_strength = info->transfer_strength;
                color_strength = info->color_strength;
            }
            if (info->flags & LMXXF_NR_FRAME_FLAG_DEBUG_VIEW)
            {
                debug_view = info->debug_view;
            }
            if (info->model_scale > 0.1f && info->model_scale <= 2.0f)
            {
                model_scale = info->model_scale;
            }
        }
        if (!(info->flags & LMXXF_NR_FRAME_FLAG_STRENGTH))
        {
            if (const wchar_t *e = _wgetenv(L"DLSS5_STRENGTH"))
            {
                float a = 1.f, b = 1.f;
                if (swscanf(e, L"%f,%f", &a, &b) == 2 && a >= 0.f && b >= 0.f)
                {
                    transfer_strength = a;
                    color_strength = b;
                }
            }
        }
        if (debug_view == 0 && _wgetenv(L"DLSS5_DEBUG_TINT") && !wcscmp(_wgetenv(L"DLSS5_DEBUG_TINT"), L"1"))
        {
            debug_view = 4; // Tint
        }
        if (transfer_strength < 0.0f || transfer_strength > 1.0f || color_strength < 0.0f || color_strength > 1.0f)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "PrepareFrame: transfer_strength and color_strength must be in [0, 1]");

        // Match upstream auto tier: <=1280x720 -> 720, <=1600x900 -> 900, else 1080.
        // Prefer CRT _putenv so MinGW std::getenv sees "auto" (SetEnvironmentVariable alone may not).
        if (!std::getenv("DLSS5_NETWORK_HEIGHT"))
            _putenv("DLSS5_NETWORK_HEIGHT=auto");
        NativeResolveNetworkGeometry(info->color_width, info->color_height);
        // Set when PrepareFrame deliberately leaves a notice in the error slot for the host to
        // log. Declared here, before the first HIP lazy-Create block, because BOTH of those
        // blocks must skip SetError when a notice is already pending - otherwise the recreate
        // reason is clobbered and a per-frame rebuild becomes invisible in the log.
        bool keepLastError = false;
        if (!session->hipPrepared)
        {
            auto geo = NativeCurrentNetworkGeometry();
            auto opt = LmxxfProductionOptions(geo.processing_width, geo.processing_height,
                                              Utf8(session->modulesDir), Utf8(session->weightsDir));
            if (opt.graph)
                return Fail(LMXXF_NR_FAILED, "PrepareFrame: graph must stay off");
            session->bridge = new hip_reference::D3D12Bridge();
            session->bridge->Create(session->queue, opt, {});
            session->hipPrepared = true;
            char geoMsg[192] {};
            std::snprintf(geoMsg, sizeof geoMsg,
                          "lmxxf: HIP lazy Create color=%ux%u network=%ux%u (proc %ux%u)",
                          info->color_width, info->color_height, geo.valid_width, geo.valid_height,
                          geo.processing_width, geo.processing_height);
            OutputDebugStringA(geoMsg);
            OutputDebugStringA("\n");
            if (!keepLastError)
                SetError(geoMsg);
        }
        auto *color = static_cast<ID3D12Resource *>(info->color);
        if (!color)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "PrepareFrame: null color resource");
        const D3D12_RESOURCE_DESC cdesc = color->GetDesc();
        const DXGI_FORMAT cfmt = cdesc.Format;
        const UINT cw = static_cast<UINT>(cdesc.Width);
        const UINT ch = cdesc.Height;
        if (const char *why = ColorInputProblem(cdesc))
        {
            char msg[224];
            std::snprintf(msg, sizeof msg,
                          "PrepareFrame: colour rejected (%s): fmt=%u %llux%llu arr=%u mips=%u "
                          "samples=%u flags=0x%x fitLarge=%d",
                          why, unsigned(cfmt), static_cast<unsigned long long>(cdesc.Width),
                          static_cast<unsigned long long>(cdesc.Height), unsigned(cdesc.DepthOrArraySize),
                          unsigned(cdesc.MipLevels), unsigned(cdesc.SampleDesc.Count), unsigned(cdesc.Flags),
                          NativeFitLargeInput() ? 1 : 0);
            return Fail(LMXXF_NR_INVALID_ARGUMENT, msg);
        }

        // Exposure is optional and sits after model_scale, so only a host whose struct_size
        // covers it supplies one. The scalars are clamped rather than trusted: a NaN or an
        // infinity would otherwise fail NativeCodecParameters::Valid() at Record time, which
        // throws and poisons the session.
        ID3D12Resource *frameExposure = nullptr;
        D3D12_RESOURCE_STATES frameExposureState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        float framePreExposure = 1.0f, frameExposureScale = 1.0f;
        if (info->struct_size >= sizeof(LmxxfNrFrameInfo))
        {
            frameExposure = static_cast<ID3D12Resource *>(info->exposure);
            frameExposureState = static_cast<D3D12_RESOURCE_STATES>(info->exposure_state);
            if (info->pre_exposure > 0.0f && info->pre_exposure < 1.0e6f)
                framePreExposure = info->pre_exposure;
            if (info->exposure_scale > 0.0f && info->exposure_scale < 1.0e6f)
                frameExposureScale = info->exposure_scale;
        }
        // Exposure is an enhancement, not a requirement. The codec samples Texture2D<float> at
        // (0,0), so it can only use a 1x1 R16/R32 float; anything else must cost the game its
        // exposure, not the whole frame.
        if (frameExposure)
        {
            const D3D12_RESOURCE_DESC ed = frameExposure->GetDesc();
            const bool exposureOk = ed.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && ed.Width == 1 &&
                                    ed.Height == 1 && ed.MipLevels == 1 && ed.DepthOrArraySize == 1 &&
                                    ed.SampleDesc.Count == 1 &&
                                    (ed.Format == DXGI_FORMAT_R16_FLOAT || ed.Format == DXGI_FORMAT_R32_FLOAT) &&
                                    !(ed.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
            if (!exposureOk)
            {
                // Atomic: sessions on different threads share it.
                static std::atomic<unsigned> exposureNotices {0};
                const unsigned notice = exposureNotices.fetch_add(1, std::memory_order_relaxed);
                if (notice < 3 || (notice % 300) == 0)
                {
                    char msg[224];
                    std::snprintf(msg, sizeof msg,
                                  "PrepareFrame: exposure unusable, continuing without it "
                                  "(fmt=%u %llux%llu arr=%u mips=%u samples=%u flags=0x%x)",
                                  unsigned(ed.Format), static_cast<unsigned long long>(ed.Width),
                                  static_cast<unsigned long long>(ed.Height), unsigned(ed.DepthOrArraySize),
                                  unsigned(ed.MipLevels), unsigned(ed.SampleDesc.Count), unsigned(ed.Flags));
                    OutputDebugStringA(msg);
                    OutputDebugStringA("\n");
                    SetError(msg);
                    keepLastError = true;
                }
                frameExposure = nullptr;
                frameExposureState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            }
        }
        // Choose what the codecs will bind: our stable copy if this frame has a usable source,
        // otherwise nothing. Rebuilding follows a change of THAT, not of the game's pointer.
        ID3D12Resource *bindExposure = nullptr;
        // A copy replaced below while the codecs are alive. The codecs hold an SRV to it and
        // frames already submitted may still read it, so it is only released once the drain in
        // the rebuild below has succeeded. Keeping it alive until then also keeps its address
        // out of reuse: a new copy allocated at the same address would compare equal to
        // boundExposure, skip the rebuild and leave the codecs reading a freed resource.
        ID3D12Resource *retiredExposure = nullptr;
        if (frameExposure)
        {
            const DXGI_FORMAT srcFormat = frameExposure->GetDesc().Format;
            if (!session->exposureCopy || session->exposureCopyFormat != srcFormat)
            {
                if (session->exposureCopy)
                {
                    // Without codecs nothing references it: every path that tears the chain
                    // down drains the GPU first.
                    if (session->encode)
                        retiredExposure = session->exposureCopy;
                    else
                        session->exposureCopy->Release();
                    session->exposureCopy = nullptr;
                }
                D3D12_RESOURCE_DESC cd {};
                cd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                cd.Width = cd.Height = 1;
                cd.DepthOrArraySize = cd.MipLevels = 1;
                cd.Format = srcFormat;
                cd.SampleDesc.Count = 1;
                D3D12_HEAP_PROPERTIES hp {};
                hp.Type = D3D12_HEAP_TYPE_DEFAULT;
                if (SUCCEEDED(session->device->CreateCommittedResource(
                        &hp, D3D12_HEAP_FLAG_NONE, &cd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        nullptr, IID_PPV_ARGS(&session->exposureCopy))))
                {
                    session->exposureCopyFormat = srcFormat;
                }
                else
                {
                    session->exposureCopy = nullptr;
                    session->exposureCopyFormat = DXGI_FORMAT_UNKNOWN;
                    frameExposure = nullptr;
                }
            }
            bindExposure = session->exposureCopy;
        }
        // Compare like with like: the render subrect (info->color_width/height, remembered in
        // job.width/height) against the previous subrect, and the Color texture allocation
        // (cw/ch from GetDesc) against the previous allocation. Comparing the remembered
        // SUBRECT against the ALLOCATION made any title whose buffer is larger than its render
        // area rebuild the codec chain on every frame.
        const bool exposureChanged = session->encode && session->boundExposure != bindExposure;
        const bool validChanged =
            session->encode && (session->job.width != info->color_width ||
                                session->job.height != info->color_height);
        const bool formatChanged = session->encode && session->colorFormat != cfmt;
        const bool allocChanged = session->encode &&
                                  ((session->allocWidth && cw != session->allocWidth) ||
                                   (session->allocHeight && ch != session->allocHeight));
        const bool geoChanged = exposureChanged || validChanged || formatChanged || allocChanged;
        bool keepRecreateLog = false;
        if (session->encode && geoChanged)
        {
            ++session->codecRecreates;
            char reason[96] {};
            std::snprintf(reason, sizeof reason, "%s%s%s%s", exposureChanged ? "exposure+" : "",
                          validChanged ? "valid+" : "", formatChanged ? "format+" : "",
                          allocChanged ? "alloc+" : "");
            size_t rlen = std::strlen(reason);
            if (rlen && reason[rlen - 1] == '+')
                reason[rlen - 1] = 0;
            if (!reason[0])
                std::snprintf(reason, sizeof reason, "unknown");
            char msg[320] {};
            std::snprintf(msg, sizeof msg,
                          "lmxxf: codec recreate #%u reason=%s valid=%ux%u->%ux%u alloc=%ux%u fmt=%u->%u",
                          session->codecRecreates, reason, session->job.width, session->job.height,
                          info->color_width, info->color_height, cw, ch,
                          static_cast<unsigned>(session->colorFormat), static_cast<unsigned>(cfmt));
            OutputDebugStringA(msg);
            OutputDebugStringA("\n");
            SetError(msg);
            keepLastError = true;
        }
        const bool pointerChanged = session->encode && color != session->job.color;

        if (session->encode && geoChanged)
        {
            if (FAILED(session->DrainGpu()))
                return Fail(LMXXF_NR_UNAVAILABLE,
                            "PrepareFrame: color geometry change; GPU drain failed (retry or rebuild session)");
            session->TeardownCodecChain();
            session->job = {};
        }
        // Reached only after any drain above succeeded (a failed drain returns and leaks it,
        // fail-closed). Without a rebuild the retired copy was never bound: a new copy cannot
        // share its address while it lives, so no rebuild means both bindings were null.
        if (retiredExposure)
            retiredExposure->Release();

        if (!session->encode)
        {
            if (!session->bridge)
            {
                auto geo = NativeCurrentNetworkGeometry();
                auto opt = LmxxfProductionOptions(geo.processing_width, geo.processing_height,
                                                  Utf8(session->modulesDir), Utf8(session->weightsDir));
                if (opt.graph)
                    return Fail(LMXXF_NR_FAILED, "PrepareFrame: graph must stay off");
                session->bridge = new hip_reference::D3D12Bridge();
                session->bridge->Create(session->queue, opt, {});
                session->hipPrepared = true;
                char geoMsg[192] {};
                std::snprintf(geoMsg, sizeof geoMsg,
                              "lmxxf: HIP lazy Create color=%ux%u network=%ux%u (proc %ux%u)",
                              info->color_width, info->color_height, geo.valid_width, geo.valid_height,
                              geo.processing_width, geo.processing_height);
                OutputDebugStringA(geoMsg);
                OutputDebugStringA("\n");
                if (!keepLastError)
                    SetError(geoMsg);
            }
            // One warm-up dispatch before recording so lazy weight and module uploads cannot
            // land inside the producer-wait callback later. Only reached after the colour
            // contract passed, so a title we cannot serve never pays for it.
            session->bridge->PrepareStagedKernels();
            NativeGameCodec *enc = nullptr;
            NativeGameRgbInput *rgbIn = nullptr;
            NativeRgbTexture *rgbOut = nullptr;
            NativeGameCodec *dec = nullptr;
            ID3D12Resource *disp = nullptr;
            try
            {
                // RGB9E5 is only accepted when the codec keeps a private FP16 output: that
                // format cannot be a UAV target and is never written, only read. Driven by the
                // input format rather than game identity; every other format keeps its existing
                // output route.
                const bool privateFloatOutput = (cfmt == DXGI_FORMAT_R9G9B9E5_SHAREDEXP);
                enc = new NativeGameCodec();
                session->boundExposure = bindExposure;
                session->allocWidth = cw;
                session->allocHeight = ch;
                enc->Create(session->device, {color}, session->shaderDir, privateFloatOutput, bindExposure);
                rgbIn = new NativeGameRgbInput();
                rgbIn->Create(session->device, enc->Output(), session->shaderDir);
                rgbOut = new NativeRgbTexture();
                rgbOut->Create(session->device, session->bridge->Output(), session->shaderDir);
                dec = new NativeGameCodec();
                dec->Create(session->device, {enc->Output(), rgbOut->Output(), color}, session->shaderDir,
                            privateFloatOutput, bindExposure);
                if (dec->BufferOutput())
                {
                    D3D12_RESOURCE_DESC td = cdesc;
                    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                    D3D12_HEAP_PROPERTIES hp {};
                    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
                    const HRESULT chr = session->device->CreateCommittedResource(
                        &hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        nullptr, IID_PPV_ARGS(&disp));
                    if (FAILED(chr) || !disp)
                        throw std::runtime_error("decode display texture create failed");
                }
            }
            catch (...)
            {
                if (disp)
                    disp->Release();
                delete dec;
                delete rgbOut;
                delete rgbIn;
                delete enc;
                throw;
            }
            session->encode = enc;
            session->rgbInput = rgbIn;
            session->rgbTex = rgbOut;
            session->decode = dec;
            session->decodeDisplay = disp;
        }
        else if (pointerChanged)
        {
            const bool needDrain = session->encode->RebindNeedsCompletion(0, color) ||
                                   (session->decode && session->decode->RebindNeedsCompletion(2, color));
            if (needDrain && FAILED(session->DrainGpu()))
                return Fail(LMXXF_NR_UNAVAILABLE,
                            "PrepareFrame: color rebind needs GPU completion (drain failed; host rebuild)");
            try
            {
                session->encode->RebindInputAfterCompletion(0, color);
                if (session->decode)
                    session->decode->RebindInputAfterCompletion(2, color);
            }
            catch (const std::exception &ex)
            {
                SetError(ex.what());
                if (FAILED(session->DrainGpu()))
                    return Fail(LMXXF_NR_UNAVAILABLE, "PrepareFrame: rebind threw; GPU drain failed");
                session->TeardownCodecChain();
                session->job = {};
                return Fail(LMXXF_NR_UNAVAILABLE,
                            "PrepareFrame: color rebind failed after drain; chain torn down (retry)");
            }
        }

        session->job = {};
        session->job.color = color;
        session->job.colorState = static_cast<D3D12_RESOURCE_STATES>(info->color_state);
        session->job.width = info->color_width;
        session->job.height = info->color_height;
        session->job.transfer_strength = transfer_strength;
        session->job.color_strength = color_strength;
        session->job.debug_view = debug_view;
        session->job.pre_exposure = framePreExposure;
        session->job.exposure_scale = frameExposureScale;
        session->job.sourceExposure = frameExposure;
        session->job.sourceExposureState = frameExposureState;
        session->job.codec_passthrough = (info->flags & LMXXF_NR_FRAME_FLAG_CODEC_PASSTHROUGH) != 0;
        session->colorFormat = cfmt;
        session->job.seed = 1;
        session->job.state = LMXXF_NR_JOB_PREPARED;
        job->handle = &session->job;
        if (!session->decode)
            return Fail(LMXXF_NR_FAILED, "PrepareFrame: decode missing");
        job->private_output = session->decode->BufferOutput()
                                   ? static_cast<void *>(session->decodeDisplay)
                                   : static_cast<void *>(session->decode->Output());
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}

int32_t RecordInputs(void *context, void *job, void *command_list)
{
    auto *session = static_cast<Session *>(context);
    return GuardSession(session, [&] {
        if (!session)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "RecordInputs: null context");
        if (!session->hipPrepared)
            return Fail(LMXXF_NR_NOT_IMPLEMENTED, "RecordInputs is not wired (HIP/codec next)");
        RequireSession(session);
        auto *list = static_cast<ID3D12GraphicsCommandList *>(command_list);
        auto *j = static_cast<Job *>(job ? job : &session->job);
        if (!list || !j)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "RecordInputs: need job and command list");
        if (j->state != LMXXF_NR_JOB_PREPARED)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "RecordInputs: job not in PREPARED state");
        ListContract(session, list);

        // Refresh our stable exposure copy from the game's texture. Both textures share a format
        // (CopyTextureRegion requires it), and we leave the copy in NON_PIXEL_SHADER_RESOURCE so
        // the codec's own exposure transition is a no-op.
        if (session->boundExposure && j->sourceExposure)
        {
            // A source already in COPY_SOURCE needs no transition, and a barrier whose before and
            // after states are equal is invalid - so it only joins the batch when it moves.
            const bool moveSource = j->sourceExposureState != D3D12_RESOURCE_STATE_COPY_SOURCE;
            const UINT nb = moveSource ? 2u : 1u;
            D3D12_RESOURCE_BARRIER b[2] {};
            b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b[0].Transition = {session->exposureCopy, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST};
            b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b[1].Transition = {j->sourceExposure, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                               j->sourceExposureState, D3D12_RESOURCE_STATE_COPY_SOURCE};
            list->ResourceBarrier(nb, b);
            D3D12_TEXTURE_COPY_LOCATION dst {}, src {};
            dst.pResource = session->exposureCopy;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.pResource = j->sourceExposure;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            b[1].Transition.StateAfter = j->sourceExposureState;
            list->ResourceBarrier(nb, b);
        }
        NativeCodecParameters encParams = NativeGameCodec::LegacyParameters();
        encParams.pre_exposure = j->pre_exposure;
        encParams.exposure_scale = j->exposure_scale;
        session->encode->Record(list, session->CodecStates({j->colorState}), 1.f, encParams);
        if (j->codec_passthrough)
        {
            // Bypass HIP: Copy encoder output directly to rgbTex output so decoder receives it as neural input.
            ID3D12Resource *src = session->encode->Output();
            ID3D12Resource *dst = session->rgbTex->Output();
            D3D12_RESOURCE_BARRIER barriers[2] {};
            barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[0].Transition = {src, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                      D3D12_RESOURCE_STATE_COPY_SOURCE};
            barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[1].Transition = {dst, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                      D3D12_RESOURCE_STATE_COPY_DEST};
            list->ResourceBarrier(2, barriers);
            list->CopyResource(dst, src);
            std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
            std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
            list->ResourceBarrier(2, barriers);
            j->state = LMXXF_NR_JOB_PRODUCER_SUBMITTED;
            SetError("");
            return static_cast<int32_t>(LMXXF_NR_OK);
        }
        session->rgbInput->Record(list, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        session->bridge->RecordInputCopy(list, session->rgbInput->PostBase(), nullptr);
        j->state = LMXXF_NR_JOB_PRODUCER_SUBMITTED;
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}

int32_t EnqueueHip(void *context, void *job, void *command_queue)
{
    auto *session = static_cast<Session *>(context);
    return GuardSession(session, [&] {
        if (!session)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "EnqueueHip: null context");
        if (!session->hipPrepared)
            return Fail(LMXXF_NR_NOT_IMPLEMENTED, "EnqueueHip is not wired (HIP/codec next)");
        RequireSession(session);
        if (session->weightsDir.empty())
            return Fail(LMXXF_NR_UNAVAILABLE,
                        "EnqueueHip: weights not found (set LMXXF_WEIGHTS_DIR to tiled assets, not 0.24.2 HIP/)");
        auto *j = static_cast<Job *>(job ? job : &session->job);
        if (!j)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "EnqueueHip: null job");
        if (j->state != LMXXF_NR_JOB_PRODUCER_SUBMITTED && j->state != LMXXF_NR_JOB_CONSUMER_COMPLETE)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "EnqueueHip: job not in PRODUCER_SUBMITTED or CONSUMER_COMPLETE state");
        if (j->codec_passthrough)
        {
            if (j->state == LMXXF_NR_JOB_PRODUCER_SUBMITTED)
                j->state = LMXXF_NR_JOB_NR_COMPLETE;
            SetError("");
            return static_cast<int32_t>(LMXXF_NR_OK);
        }
        auto *targetQueue = static_cast<ID3D12CommandQueue *>(command_queue ? command_queue : session->queue);
        const bool queueMatch = !session->queue || NativeSameDevice(targetQueue, session->queue);
        if (!queueMatch)
        {
            if (!session->zeroOutputFallback)
                throw std::runtime_error("EnqueueHip: command queue does not match session queue");
            ID3D12Device *targetDevice = nullptr;
            if (!targetQueue || FAILED(targetQueue->GetDevice(IID_PPV_ARGS(&targetDevice))) || !targetDevice)
                throw std::runtime_error("EnqueueHip: cannot query fallback queue device");
            const bool sameDevice = NativeSameDevice(targetDevice, session->device);
            targetDevice->Release();
            if (!sameDevice)
                throw std::runtime_error("EnqueueHip: fallback queue device mismatch");
            // Queue mismatch: cannot synchronize HIP with targetQueue on this session.
            // Complete old readers and the target queue's submitted producer
            // before a HIP zero write. A D3D12 zero copy is also ordered here.
            if (FAILED(session->DrainGpu(kSubmissionWaitMs)) ||
                FAILED(session->DrainQueue(targetQueue, kSubmissionWaitMs)))
            {
                session->failed = true;
                return Fail(LMXXF_NR_FAILED, "EnqueueHip: producer or old session queue did not drain before fallback clear");
            }
            // A zero neural output makes the normal decoder view use original Color.
            const bool cleared = session->bridge && session->bridge->ClearOutput(targetQueue);
            if (cleared)
            {
                targetQueue->AddRef();
                session->fallbackConsumerQueue = targetQueue;
                if (j->state == LMXXF_NR_JOB_PRODUCER_SUBMITTED)
                    j->state = LMXXF_NR_JOB_NR_COMPLETE;
                SetError("EnqueueHip: queue mismatch; output zeroed; normal decoder view uses original Color");
                return static_cast<int32_t>(LMXXF_NR_OK);
            }
            else
            {
                session->failed = true;
                SetError("EnqueueHip: queue mismatch and output clear failed; cannot guarantee clean visual fallback");
                return static_cast<int32_t>(LMXXF_NR_FAILED);
            }
        }
        QueueContract(session, targetQueue);
        try
        {
            session->bridge->EnqueueAfterProducer(targetQueue, j->seed, false);
            if (j->state == LMXXF_NR_JOB_PRODUCER_SUBMITTED)
                j->state = LMXXF_NR_JOB_NR_COMPLETE;
            SetError("");
            return static_cast<int32_t>(LMXXF_NR_OK);
        }
        catch (const std::exception &ex)
        {
            if (!session->zeroOutputFallback)
                throw;
            const bool cleared = session->bridge && session->bridge->ClearOutput(targetQueue);
            if (cleared)
            {
                if (j->state == LMXXF_NR_JOB_PRODUCER_SUBMITTED)
                    j->state = LMXXF_NR_JOB_NR_COMPLETE;
                std::string msg = "EnqueueHip: enqueue failed (";
                msg += ex.what();
                msg += "); output zeroed; normal decoder view uses original Color";
                SetError(msg.c_str());
                return static_cast<int32_t>(LMXXF_NR_OK);
            }
            else
            {
                session->failed = true;
                std::string msg = "EnqueueHip: enqueue failed (";
                msg += ex.what();
                msg += ") and clear failed; cannot guarantee clean visual fallback";
                SetError(msg.c_str());
                return static_cast<int32_t>(LMXXF_NR_FAILED);
            }
        }
    });
}

int32_t RecordOutputs(void *context, void *job, void *command_list)
{
    auto *session = static_cast<Session *>(context);
    return GuardSession(session, [&] {
        if (!session)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "RecordOutputs: null context");
        if (!session->hipPrepared)
            return Fail(LMXXF_NR_NOT_IMPLEMENTED, "RecordOutputs is not wired (HIP/codec next)");
        RequireSession(session);
        auto *list = static_cast<ID3D12GraphicsCommandList *>(command_list);
        auto *j = static_cast<Job *>(job ? job : &session->job);
        if (!list || !j)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "RecordOutputs: need job and command list");
        if (j->state != LMXXF_NR_JOB_NR_COMPLETE && j->state != LMXXF_NR_JOB_PRODUCER_SUBMITTED)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "RecordOutputs: job not in NR_COMPLETE or PRODUCER_SUBMITTED state");
        ListContract(session, list);

        if (!j->codec_passthrough)
        {
            session->bridge->RecordOutputReadable(list);
            session->rgbTex->Record(list);
        }
        if (!session->decode)
            return Fail(LMXXF_NR_FAILED, "RecordOutputs: decode missing");
        NativeCodecParameters codecParams;
        codecParams.pre_exposure = j->pre_exposure;
        codecParams.exposure_scale = j->exposure_scale;
        codecParams.transfer_strength = j->transfer_strength;
        codecParams.color_strength = j->color_strength;
        codecParams.debug_view = static_cast<NativeCodecDebugView>(j->debug_view);
        session->decode->Record(list,
                                session->CodecStates({D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                      j->colorState}),
                                1.f, codecParams);
        if (session->decode->BufferOutput())
        {
            if (!session->decodeDisplay)
                return Fail(LMXXF_NR_FAILED, "RecordOutputs: decode display missing");
            ID3D12Resource *src = session->decode->Output();
            ID3D12Resource *dst = session->decodeDisplay;
            D3D12_RESOURCE_BARRIER barriers[2] {};
            barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[0].Transition = {src, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                      D3D12_RESOURCE_STATE_COPY_SOURCE};
            barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[1].Transition = {dst, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                      D3D12_RESOURCE_STATE_COPY_DEST};
            list->ResourceBarrier(2, barriers);
            D3D12_TEXTURE_COPY_LOCATION dstLoc {};
            dstLoc.pResource = dst;
            dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION srcLoc {};
            srcLoc.pResource = src;
            srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            const auto &geo = session->decode->Geometry();
            const DXGI_FORMAT fmt = NativeViewFormat(dst->GetDesc().Format);
            const bool bytes4 = NativeIsRgba8Unorm(fmt) || NativeIsR11G11B10(fmt);
            srcLoc.PlacedFootprint.Footprint.Format = fmt;
            srcLoc.PlacedFootprint.Footprint.Width = geo.width;
            srcLoc.PlacedFootprint.Footprint.Height = geo.height;
            srcLoc.PlacedFootprint.Footprint.Depth = 1;
            srcLoc.PlacedFootprint.Footprint.RowPitch = geo.RowPitch(bytes4 ? 4u : 8u);
            list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
            std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
            std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
            list->ResourceBarrier(2, barriers);
        }
        j->state = LMXXF_NR_JOB_CONSUMER_COMPLETE;
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}

int32_t ExecuteAfterProducer(void *context, void *job, void *command_queue)
{
    return EnqueueHip(context, job, command_queue);
}

int32_t CancelUnsubmitted(void *context, void *job)
{
    auto *session = static_cast<Session *>(context);
    return GuardSession(session, [&] {
        if (!session)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "null context");
        auto *j = static_cast<Job *>(job ? job : &session->job);
        if (j)
            j->state = LMXXF_NR_JOB_RETIRED;
        if (session->bridge)
            session->bridge->CancelUnsubmitted();
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}
int32_t Poll(void *context, void *job, uint32_t *state)
{
    return Guard([&] {
        auto *session = static_cast<Session *>(context);
        if (!session)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "null context");
        auto *j = static_cast<Job *>(job ? job : &session->job);
        if (state)
            *state = j ? j->state : LMXXF_NR_JOB_NONE;
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}
int32_t Retire(void *context, void *job)
{
    auto *session = static_cast<Session *>(context);
    return GuardSession(session, [&] {
        if (!session)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "null context");
        auto *j = static_cast<Job *>(job ? job : &session->job);
        if (j)
            j->state = LMXXF_NR_JOB_RETIRED;
        if (session->bridge)
            session->bridge->NotifyOutputSubmittedIfRecorded(session->queue);
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}
int32_t ResetHistory(void *context)
{
    auto *session = static_cast<Session *>(context);
    return GuardSession(session, [&] {
        if (!session)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "null context");
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}
int32_t Drain(void *context)
{
    auto *session = static_cast<Session *>(context);
    return GuardSession(session, [&] {
        if (!session)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "null context");
        const HRESULT hr = session->DrainGpu();
        if (FAILED(hr))
            return Fail(LMXXF_NR_FAILED, "Drain: GPU wait failed or timed out");
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}

int32_t GetStatus(void *context, char *buf, uint32_t buf_chars)
{
    return Guard([&] {
        if (!buf || buf_chars == 0)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "GetStatus: empty buffer");
        auto *session = static_cast<Session *>(context);
        char text[256] {};
        if (!session)
            std::snprintf(text, sizeof text, "no session");
        else if (session->failed)
            std::snprintf(text, sizeof text, "lmxxf poisoned (fatal error)");
        else if (!session->modulesValidated)
            std::snprintf(text, sizeof text, "lmxxf runtime stub (no modules path)");
        else
            if (session->hipPrepared && NativeNetworkGeometryResolved())
            {
                auto geo = NativeCurrentNetworkGeometry();
                std::snprintf(text, sizeof text,
                              "lmxxf modules_ok=%u hip=1 net=%ux%u color_job=%ux%u weights=%u recreates=%u",
                              static_cast<unsigned>(session->hsacoCount), geo.valid_width, geo.valid_height,
                              session->job.width, session->job.height,
                              session->weightsDir.empty() ? 0u : 1u, session->codecRecreates);
            }
            else
            {
                std::snprintf(text, sizeof text, "lmxxf modules_ok=%u hip=0 prepared=%u queue=%u weights=%u recreates=%u",
                              static_cast<unsigned>(session->hsacoCount), session->hipPrepared ? 1u : 0u,
                              session->queueBound ? 1u : 0u,
                              session->weightsDir.empty() ? 0u : 1u, session->codecRecreates);
            }
        std::strncpy(buf, text, buf_chars - 1);
        buf[buf_chars - 1] = 0;
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}

int32_t GetLastError(char *buf, uint32_t buf_chars)
{
    return Guard([&] {
        if (!buf || buf_chars == 0)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "GetLastError: empty buffer");
        std::strncpy(buf, g_lastError, buf_chars - 1);
        buf[buf_chars - 1] = 0;
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}
} // namespace

extern "C" int32_t LmxxfNrGetApi(uint32_t abi_version, LmxxfNrApi *out)
{
    return Guard([&] {
        if (!out)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "GetApi: null out");
        if (out->struct_size != sizeof(LmxxfNrApi))
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "GetApi: struct_size mismatch");
        if (abi_version != LMXXF_NR_ABI_VERSION)
            return Fail(LMXXF_NR_UNSUPPORTED_ABI, "GetApi: unsupported abi_version");
        std::memset(out, 0, sizeof(*out));
        out->struct_size = sizeof(LmxxfNrApi);
        out->abi_version = LMXXF_NR_ABI_VERSION;
        out->QueryCapabilities = QueryCapabilities;
        out->Create = Create;
        out->Destroy = Destroy;
        out->PrepareSession = PrepareSession;
        out->PrepareFrame = PrepareFrame;
        out->RecordInputs = RecordInputs;
        out->EnqueueHip = EnqueueHip;
        out->RecordOutputs = RecordOutputs;
        out->ExecuteAfterProducer = ExecuteAfterProducer;
        out->CancelUnsubmitted = CancelUnsubmitted;
        out->Poll = Poll;
        out->Retire = Retire;
        out->ResetHistory = ResetHistory;
        out->Drain = Drain;
        out->GetStatus = GetStatus;
        out->GetLastError = GetLastError;
        SetError("");
        return static_cast<int32_t>(LMXXF_NR_OK);
    });
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
