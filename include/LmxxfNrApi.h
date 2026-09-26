#pragma once

/* Versioned C ABI for LmxxfNrRuntime.dll.
 * MSVC host and MinGW runtime must not share a C++ ABI. No STL, exceptions, or
 * CRT-allocated objects cross this boundary. x64 stdcall is the Windows default. */

#include <stdint.h>
#include <stddef.h> /* wchar_t in C hosts */

#ifdef __cplusplus
extern "C" {
#endif

#define LMXXF_NR_ABI_VERSION 1u
/* sizeof() of an ABI v1 LmxxfNrFrameInfo: it stopped at model_scale, before the exposure
 * fields. A host talking to a runtime that predates them sends this as struct_size. */
#define LMXXF_NR_FRAME_INFO_V1_SIZE 80u

/* Optional recovery when HIP enqueue or the session queue contract fails.
 * On recovery, EnqueueHip returns OK only after the private neural output was fully zeroed;
 * GetLastError then contains a recovery diagnostic. Submit input and output work
 * on the queue passed to EnqueueHip, and submit the output reader before Retire,
 * Drain, or Destroy. The runtime waits for that supplied queue before output reuse
 * or destruction. It cannot discover output readers on other queues; callers must
 * synchronize those queues themselves before reuse or destruction. A failed or
 * uncertain clear returns FAILED and the session must be rebuilt. */
#define LMXXF_NR_CREATE_FLAG_ZERO_OUTPUT_FALLBACK (1u << 0)

enum LmxxfNrStatus
{
    LMXXF_NR_OK = 0,
    LMXXF_NR_UNSUPPORTED_ABI = 1,
    LMXXF_NR_INVALID_ARGUMENT = 2,
    LMXXF_NR_NOT_IMPLEMENTED = 3,
    LMXXF_NR_UNAVAILABLE = 4,
    LMXXF_NR_FAILED = 5
};

enum LmxxfNrJobState
{
    LMXXF_NR_JOB_NONE = 0,
    LMXXF_NR_JOB_PREPARED = 1,           /* Set by PrepareFrame; ready for RecordInputs */
    LMXXF_NR_JOB_PRODUCER_SUBMITTED = 2, /* Set by RecordInputs; producer recorded/submitted */
    LMXXF_NR_JOB_NR_ENQUEUED = 3,        /* EnqueueHip scheduled on queue */
    LMXXF_NR_JOB_NR_COMPLETE = 4,        /* EnqueueHip executed or completed */
    LMXXF_NR_JOB_CONSUMER_COMPLETE = 5,  /* Set by RecordOutputs; consumer recorded */
    LMXXF_NR_JOB_RETIRED = 6             /* Set by Retire or CancelUnsubmitted */
};

typedef struct LmxxfNrCapabilities
{
    uint32_t struct_size;
    uint32_t abi_version;
    /* The always-admitted box. Admission is by pixel budget, so a wider input (up to 2560, height
     * still within max_input_height) is also accepted while width*height stays within
     * max_input_width*max_input_height (ultrawide). */
    uint32_t max_input_width;
    uint32_t max_input_height;
    uint32_t history_supported; /* first product version: 0 */
    uint32_t overlap_supported; /* first product version: 0 */
    uint32_t graph_supported;   /* first product version: 0; EnqueueHip must not graph-wait */
    uint32_t hip_ready;         /* 1 once host+hsaco are loaded */
    uint32_t gfx1201_target;    /* 1 = this binary is for gfx1201 */
} LmxxfNrCapabilities;

typedef struct LmxxfNrCreateInfo
{
    uint32_t struct_size;
    void *device; /* ID3D12Device*; not dereferenced until HIP is wired */
    void *queue;  /* ID3D12CommandQueue*; must match device when HIP is wired */
    const wchar_t *assets_directory;
    uint32_t flags; /* LMXXF_NR_CREATE_FLAG_*; unknown bits are rejected */
} LmxxfNrCreateInfo;

#define LMXXF_NR_FRAME_FLAG_STRENGTH          (1u << 0)
#define LMXXF_NR_FRAME_FLAG_DEBUG_VIEW        (1u << 1)
#define LMXXF_NR_FRAME_FLAG_CODEC_PASSTHROUGH (1u << 2)

typedef struct LmxxfNrFrameInfo
{
    uint32_t struct_size;
    uint64_t session_id;
    uint64_t frame_id;
    uint64_t list_generation;
    void *command_list; /* ID3D12GraphicsCommandList*; Record* do not Execute */
    uint32_t color_width;
    uint32_t color_height;
    void *color; /* ID3D12Resource*; required for RecordInputs */
    uint32_t color_state; /* D3D12_RESOURCE_STATES at RecordInputs */
    uint32_t flags; /* LMXXF_NR_FRAME_FLAG_* (0 in legacy ABI v1) */
    float transfer_strength; /* Detail strength: 0..1, default 1.0 */
    float color_strength;    /* Colour strength: 0..1, default 1.0 */
    uint32_t debug_view;     /* 0=normal, 1=proxy, 2=neural solo, 3=diff 20x, 4=tint */
    float model_scale;       /* 0.25..1.0, default 1.0 */
    /* Optional exposure (ABI growth; LMXXF_NR_ABI_VERSION is unchanged because the function
     * table is not). struct_size negotiates this: a host whose struct_size stops before these
     * fields simply does not supply them, and the runtime falls back to no exposure and the
     * scalars below at their defaults. */
    void *exposure;    /* ID3D12Resource* 1x1 R16_FLOAT/R32_FLOAT, shader-readable; NULL = none */
    uint32_t exposure_state; /* D3D12_RESOURCE_STATES of exposure at RecordInputs */
    float pre_exposure;   /* game pre-exposure; finite and > 0, default 1 */
    float exposure_scale; /* exposure scale; finite and > 0, default 1 */
} LmxxfNrFrameInfo;

typedef struct LmxxfNrJob
{
    uint32_t struct_size;
    void *handle;
    void *private_output; /* ID3D12Resource* for SR; null until PrepareFrame succeeds */
} LmxxfNrJob;

typedef struct LmxxfNrApi
{
    uint32_t struct_size;
    uint32_t abi_version;
    int32_t (*QueryCapabilities)(LmxxfNrCapabilities *out);
    int32_t (*Create)(const LmxxfNrCreateInfo *info, void **context);
    int32_t (*Destroy)(void *context);
    int32_t (*PrepareSession)(void *context);
    int32_t (*PrepareFrame)(void *context, const LmxxfNrFrameInfo *info, LmxxfNrJob *job);
    int32_t (*RecordInputs)(void *context, void *job, void *command_list);
    int32_t (*EnqueueHip)(void *context, void *job, void *command_queue);
    int32_t (*RecordOutputs)(void *context, void *job, void *command_list);
    int32_t (*ExecuteAfterProducer)(void *context, void *job, void *command_queue);
    int32_t (*CancelUnsubmitted)(void *context, void *job);
    int32_t (*Poll)(void *context, void *job, uint32_t *state);
    int32_t (*Retire)(void *context, void *job);
    int32_t (*ResetHistory)(void *context);
    int32_t (*Drain)(void *context);
    int32_t (*GetStatus)(void *context, char *buf, uint32_t buf_chars);
    int32_t (*GetLastError)(char *buf, uint32_t buf_chars);
} LmxxfNrApi;

#ifdef _WIN32
#ifdef LMXXF_NR_RUNTIME_EXPORTS
#define LMXXF_NR_EXPORT __declspec(dllexport)
#else
#define LMXXF_NR_EXPORT __declspec(dllimport)
#endif
#else
#define LMXXF_NR_EXPORT
#endif

/* Sole export. Caller sets out->struct_size = sizeof(LmxxfNrApi) before the call. */
LMXXF_NR_EXPORT int32_t LmxxfNrGetApi(uint32_t abi_version, LmxxfNrApi *out);

#ifdef __cplusplus
}
#endif
