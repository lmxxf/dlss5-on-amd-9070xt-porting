# Optional integration interfaces (PR 12)

Based on upstream `297b032ac55f005d78568e684f30608651044f62`. All new
integration modes are opt-in. The addon now includes an explicitly opt-in FFX
pre-upscale Fast History consumer; its default rendering route is unchanged.
No end-to-end game validation or completed downstream release is claimed.

## Ownership and defaults

- `NativeGameCodec`, `NativeGameRgbInput`, and `NativeRgbTexture` accept
  `EnableReplayableRecording()` before creation. Only this mode changes their
  initial resource state and makes first/discarded/replayed recording barriers
  identical. `PinRecording()` adds references to the exact resources, descriptor
  heap, root signature and pipeline used by that recording. Callers release them
  only after abandoning the list or proving completion of every execution.
- `D3D12Bridge::EnableRecordingLeases()` opts into separate recording and
  execution. Record input/output, then `SealRecordedOutput()` after the last
  reader. Call `BeginRecordedExecution()` before submitting the actual producer,
  enqueue HIP after producer submission, and call `EndRecordedExecution()` with
  actual submission facts. Methods must be serialized. A completion signal
  failure retains resources. A queue change waits for the last actual consumer.
  Graph and input-poll modes are rejected for replay; legacy submission is unchanged.
- `RequestDirectHistory()` makes shared history writable. Producers leave it in
  COMMON; consumers use the bridge's existing semaphore/queue ordering.
- `RequestPostAuxiliary(row)` enables an additional raster logit output.
  `PostAuxiliary()` describes the resource, offset, capacity, processing dimensions
  and 8-byte pixel stride: FP32 logit followed by its half-RTZ diagnostic value.
  The row has exactly 32 finite coefficients. Unsupported layouts or missing exports
  fail explicitly. The actual selected norm900 module is checked too; no silent
  downgrade to a different numerical module. Without this request, allocation and
  dispatch paths retain upstream behavior.
- `SetAdaptiveReuseAllowed()` is instance-local and does not write environment
  variables or preferences. History callers must disable approximate reuse,
  including their priming frames. Existing addon hotkeys remain supported.
- Codec active width/height default to the whole resource. A smaller active area
  preserves allocation padding. `SetTypelessRgba16View()` sets one instance's
  interpretation before creation; the default mapping remains UNORM. R10 UNORM
  and TYPELESS both retain the upstream fallback through private FP16.
- The optional `NativeShaderCompiler` argument is scoped to creation/compilation.
  Providers own compilation, includes and caches. They bypass the default cache;
  there is no global mutable callback. No provider preserves the old compiler.
- `NativeFastHistory::History` owns shader-side histories only. The caller owns
  guide resources, immutable descriptor heaps and control-buffer contents through
  GPU completion, and supplies frame/reset/seed policy. It must not combine the
  helper with the reference experiment or ViT reuse.

## Validation completed on 2026-10-06 (initial snapshot)

Run `Development/test_integration_interfaces.ps1` from an MSVC x64 shell;
`-Amd` also runs the synthetic GPU tests. Results go under `exports/`.

- Codec WARP: byte equality with the fixed upstream HDR/sRGB shaders, optional
  policies/debug flags, packed legacy formats, both R10 fallback routes and the
  actual addon conversion shader, fallback disabled in a separate process,
  rotated R10 texture rebind,
  active-area padding, discarded/replayed recording, independent typed views,
  concurrent compiler-provider isolation, History pipeline compilation.
- Module load fault injection: Style copy failure, missing optional API, load
  failure, absent optional constant, and successful ownership transfer.
- MSVC compilation of `Development/HIP/bridge_network.cpp`.
- Fast History WARP and RX 9070 XT: independent double-precision five-tap
  reference, different pre/post motion, edges, reversed depth, model feedback,
  reset/zero recovery, and reflected/partial workgroups through 3840x2176.
  These shader tests use synchronous uploads. A separate addon test pauses the
  GPU queue, records ten frames with independent controls, then checks deferred
  feedback, reset, gap and exposure behavior on WARP and RX 9070 XT.
- Four C32 recipes, both gfx1200/gfx1201: LLVM23.1.2, RowOpts,
  `-real-true16`. Existing kernel code and descriptors compared against the fixed
  upstream build: unchanged except relocation of entry offsets and PC-relative
  references to the unchanged Style constant; two new logit exports added.
- D3D12 debug layer was unavailable. gfx1200 hardware and full-network auxiliary
  output/replay were not tested. No whole-network speedup claim is made.

## Downstream adoption and validation boundary (initial snapshot)

The isolated downstream Runtime has been migrated to these interfaces and compiled
against the raw candidate headers: compiler/cache policy stays downstream;
History uses this helper; codec view selection and replay are per instance;
bridge history, diagnostics and retirement use explicit requests. This does not
advance the downstream completed upstream pin or certify all newly introduced
upstream defaults/modules. Full staged upstream review, matching module rebuild,
and downstream runtime/game verification remain necessary before adoption.

The initial snapshot did not yet have full HIP network auxiliary/replay coverage.
The follow-up below records the subsequently completed checks and their scope;
neither set of synthetic results substitutes for game visual acceptance.

## Addon Fast History

Set these restart-required flags explicitly:

```text
DLSS5_PRE_UPSCALE=1
DLSS5_FAST_HISTORY=1
DLSS5_TEMPORAL_MV_UNJITTERED=1
DLSS5_FAST_HISTORY_DEPTH_INVERTED=1
DLSS5_MULTI_PASS=1
DLSS5_HIP_GRAPH=0
DLSS5_OVERLAP=0
```

Use depth direction `0` for conventional depth and `1` for reversed depth.
The addon does not capture the FFX context depth flags, so it deliberately requires
this declaration instead of guessing. `TEMPORAL_MV_UNJITTERED=1` declares the
motion-vector convention; do not set it for jittered vectors. Only the captured
FFX pre-upscale path with a full network viewport is supported. The original
`TEMPORAL_HISTORY_EXPERIMENT` remains independent and cannot be combined with this
path. Non-HIP backends, incompatible post layouts/exports, graph/overlap and
multi-pass are rejected explicitly. To change to multiple passes, disable Fast
History and restart; the hotkey cannot silently keep one pass while showing two.

Generate `post70-history-head.f16` with the existing
`Development/HIP/experiments/post-history-gate/extract_gate.py` workflow and place
it beside the other model assets. It is the original 32-column model row,
promoted from half to float; this PR adds no weights or tuned coefficients.
Rebuild the C32 module variants to obtain the new logit exports.

The addon holds frame constants, descriptor heaps and motion/depth references
through its output fence. It resets on missing/invalid guides, frame gaps,
exposure changes and explicit reset; a missing-guide frame runs current-frame NR.
Approximate ViT reuse is gated for the entire Fast History session without
rewriting the preference. No history allocations or dispatches occur when off.
The helper exposes numerical parameters to other consumers, which own their own
reset, seed, guide conventions and UI policies.

Additional bridge services are also opt-in: `RequestReleaseMarkers()` before
creation, timing pause/epoch/tag methods, and prepared HIP passthrough for
transport diagnostics. Passthrough must not consume temporal input. Existing
input-poll/pulse behavior is retained; replay explicitly rejects input-poll.

The build script retains UTF-8 BOM for Windows PowerShell 5.1 parsing of its
non-ASCII comments; module recipes are unchanged.

R10 numerical evidence: candidate and unchanged upstream decoder outputs match
byte-for-byte on WARP and AMD. A direct format-conversion shader can differ from
both by one half ULP on AMD because the codec retains the upstream luminance
round-trip. The test bounds that difference and checks alpha exactly; it does
not alter the shader to force two different algorithms to match.

## Optional consumer policies

`Options::integration` defaults to the existing addon behavior. Consumers can
veto F8 polling or HIP input polling per instance without changing process-wide
environment variables. These are capability permissions, not new addon defaults.
`override_multi_pass_skip` supplies a parsed block set; when false, the existing
environment parser is used unchanged (no new `none` syntax).

The optional `select_module(stem, fast_numeric, module_directory)` callback selects module stems at
construction. It receives the resolved architecture-specific module directory and
the exact C32 RTZ candidate before the legacy twin
normalization. With no callback, the existing module selection and missing-twin
warnings are unchanged. Consumers own their module bundle's supported fast twins,
fallback checks and numerical policy; the callback must remain valid for the
network lifetime. No module recipe or default FAST0 output is changed here.

Explicit identity codec extents now use the same direct sampling as omitted
extents. This fixes the active-subrect interface's unintended 1:1 interpolation;
legacy callers with no active extents retain their original fit predicate.


## Final-pass auxiliary scheduling for MP1/2/3 (2026-10-07)

This extends the existing **opt-in bridge interface**, not the addon's default
rendering or its Fast History policy. `RequestPostAuxiliary(row)` now admits
MP1/2/3 in the normal non-graph `Enqueue` path. It keeps the same descriptor,
eight-byte pixel stride, coefficient validation and export checks. No new model
weights, kernels, module recipes, environment defaults or History shader algorithm
are introduced by this follow-up.

| Execution | Auxiliary/history routing |
| --- | --- |
| MP1 | One real network consumes history and emits auxiliary logits, as before |
| MP2 | First pass has no temporal prefix and no auxiliary write; second does both |
| Real MP3 | First two passes have no temporal prefix/auxiliary write; third does both |
| Predicted MP3 | First pass has neither; second is the final real network and emits logits; prediction does not synthesize third-pass logits |
| Skin protection | Logits remain those of the final real network, before skin blending |

Intermediate RGB/RGBA kernels remain available. The final real pass uses the
existing RGB+logit export. Only an instance that explicitly requested auxiliary
output changes its history routing: ordinary callers still feed their history to
every pass exactly as before. `SetMultiPass()` no longer rejects this interface
solely because the pass count changes; graph/reference-feature-tap restrictions
are retained. `HIP_MP_RAW_EXPORT` is a separate diagnostic schedule and now
explicitly rejects auxiliary admission instead of accepting an incorrect schedule;
its unrequested rendering path is unchanged.

The caller owns **one final-frame history chain**: it applies reprojection and
smoothing once to the final RGB after prediction/skin blending, then retains that
result for the next frame. The caller must serialize changes, drain outstanding
work or retain each recorded instance's resources through completion, and reset
history when pass/prediction/skin mode or input geometry changes. This interface
does not add per-pass histories, reset policy, a seed rule or UI switches upstream.
Predicted-MP3 logits are explicitly a pass-2 approximation; there is no claim of
exact equivalence to three real networks or game-tested image quality.

OptScaler(NR)'s consumer already implements the final-frame chain and retains its
INI/menu defaults, motion/depth checks, reset/seed policy and History/ViT exclusion.
The addon Fast History consumer described above **remains MP1-only and off by
default**. Removing its consumer-side guards needs separate lifecycle/metadata
work and testing; this interface change does not silently lift them.

### Reproducible validation

From an MSVC x64 shell:

```powershell
./Development/test_integration_interfaces.ps1 -Amd `
    -Assets <native-game-tiled-assets> -Modules <modules/gfx1201>
```

`-Modules` is the matching architecture directory containing the existing logit
exports. Without both paths, the suite compiles the new tests and explicitly skips
the full-network GPU run; the normal WARP/shader tests still run. The test uses a
synthetic 32-value row to verify scheduling/storage and does not distribute model
coefficients. The optional baseline build uses `TEST_LEGACY_BASELINE` against the
previous headers (304613aa).

Validation on RX 9070 XT (gfx1201): intermediate RGB/RGBA leaves every auxiliary
sentinel untouched; final output writes every pixel; MP1/2/real3/predicted3,
predicted3+skin, MP2+skin and return-to-MP1 succeed on one live instance. Repeating
the same input/history/seed produces identical RGB. Seven no-aux output hashes
match PR head 304613aa, including non-null history on each legacy pass. The
raw-export build separately checks explicit auxiliary rejection. WARP/AMD codec,
History and deferred-addon fixtures, MSVC bridge compilation and the MinGW
runtime build also pass.

The matching downstream production scheduling change additionally passed its
full GPU recording/replay/reset/old-chain tests and local CI/device release checks.
This is evidence for the implemented scope, not upstream game acceptance. No
Mochi code, post-SR History support or History/ViT co-execution is added. No gfx1200
hardware or D3D12 debug-layer validation is available. No performance benefit or
elimination of all scene-specific flicker is claimed.

Downstream pins stay in place until the author merges the complete PR and the
actual merge SHA is reviewed. Its previous zero-pin rehearsal applies to 304613aa;
this scheduling extension must also be present before removing preservation rules.
