# DLSS5@AMD RDNA4

DLSS 5 (DLSSNR) on AMD RX 9070 XT / RDNA 4.

[中文说明](README.zh-CN.md)

A from-scratch re-implementation of NVIDIA's DLSS 5 neural renderer ("DLSSNR", the 71-block Swin/ViT network shipped in
`nvngx_dlssnr.dll`) for AMD RDNA 4. The network was reverse-engineered block by block and now runs as 29 HIP modules per
architecture (gfx1201 = RX 9070 series, gfx1200 = RX 9060 series) on the HIP 7 runtime that ships with the AMD driver. The
weights are NVIDIA's, extracted from the user's own copy of the DLL; nothing of NVIDIA's is distributed here.

Pipeline in every current package: **the game renders at its (low) render resolution → our network processes that frame →
the host upscaler (FSR) takes it to the display resolution.** Three packages, three ways of getting into that position:

| Package | For | How |
|---|---|---|
| **OptiScaler** (regular) | games that expose DLSS (Stellar Blade, Lies of P, …) | OptiScaler answers the game's DLSS call with FSR; our ReShade add-on (`dlss5-amd.addon64`) runs the network on the FSR input first |
| **Magpie** (portable) | any game, no upscaler support needed | Magpie captures the game window; the network runs in the FSR3_SR slot of its effect group, then FSR4 fills the screen (optional XeSS frame generation) |
| **OptiScaler-REFramework** (RE9 only) | Resident Evil Requiem, whose command submission the regular route cannot split | TheAutomatic's modified OptiScaler host + our `LmxxfNrRuntime.dll` (matched pair; do not mix with the regular packages) |

**Current release: 0.32 (2026-09-26).** Switching resolution or DLSS preset no longer leaks VRAM (the AMD HIP driver
never returns imported shared buffers; each switch used to lose 70–90 MB, buffers are now reused per network tier). C32 input
reads as whole-line vector loads (bit-exact, about −0.9%). **The RE9 package is configurable**: its runtime reads `DLSS5_HIP_*`
and related keys from `DLSS5-AMD\native-game-flags.txt`, with the 0.31 kernels and tier rule on by default (medium settings:
2K Quality 51–52 → 54, 2K native AA 36 → 38), and includes TheAutomatic's PR #9. Download links are in the changelog table below ([Quark](https://pan.quark.cn/s/b805e071405c), plus a [Gofile mirror](https://gofile.io/d/CZ67LYIc) for users without a Chinese phone number).

**Requirements.** An RDNA 4 GPU (RX 9070 XT tested; RX 9060 kernels included, untested) and an AMD driver that ships
`amdhip64_7.dll` (current release drivers do). No HIP SDK, no Agility SDK, no preview DXC, no Windows Developer Mode. The
add-on takes ≈1.2 GB of VRAM at 900p (0.6 GB weights, 0.3 GB activations; `DLSS5_HIP_MEMORY=1` writes the breakdown to
`logs\native-hip.txt`); if VRAM is exhausted the frame rate drops and does not recover, so keep texture quality at "High" or
below in Stellar Blade.

**Configuration.** Each package ships the repository template as `DLSS5-AMD\native-game-flags.txt`
([regular](scripts/hip-game-flags.txt), [Magpie](scripts/hip-magpie-flags.txt), [RE9](scripts/hip-re9-flags.txt); keys are explained in
[scripts/CONFIGURATION.md](scripts/CONFIGURATION.md)). The network tier follows the input (an input within 110% of a tier on both axes uses it: 720 up to 1408×792,
900 up to 1760×990, else 1080, so 2K Quality 1707×961 runs at 900; `DLSS5_NETWORK_HEIGHT` forces one). The regular package turns on a lossy *adaptive ViT reuse* by default
(`DLSS5_VIT_ADAPTIVE=1`, keys in [Development/HIP/VIT-REUSE.md](Development/HIP/VIT-REUSE.md); F8 toggles it and EXACT, set 0 for
bit-exact output; Magpie and RE9 ship it off). From 0.32 the RE9 runtime also reads `DLSS5_HIP_*`, `DLSS5_SKIP_BLOCKS`,
`DLSS5_FIT_LARGE` and the related keys from the flags file; host-side options stay in `OptiScaler.ini` `[DlssNr]`.

**Magpie notes.** The `FSR3_SR` item of the bundled effect group *is* the DLSS5 entry (its UI name stays FSR3); keep it at
input size and let the following FSR4 item upscale. Use AMD optical flow only on that first item and set Optical Flow Method
to None for FSR4 and XeSS frame generation. `Alt+Shift+A` starts/stops scaling, `F6` toggles the network in every package.

**Per-game notes (regular package).** Titles that ship their own FSR dll next to the executable (REDengine: *Cyberpunk 2077*
2.31; Katana: *Wo Long 2*) need two settings, both verified on Cyberpunk 2077 on 2026-09-24: `OptiScaler.ini` `[Inputs]
EnableFfxInputs=false` (otherwise OptiScaler dispatches FSR through its own detour and the add-on never sees the call) and
`DLSS5_PRE_UPSCALE_ASYNC=0` in `native-game-flags.txt` (their colour buffer is a transient aliased resource; with deferred
submission the add-on copied whatever the memory held at that moment, a sky probe, and the picture only changed in tone).
This needs the add-on from 0.30 or later (hooks the upscaler dll directly and accepts the read-combination resource states
those engines leave behind). Wo Long 2 additionally records draws after the upscaler in the same command list, which the
regular route still rejects. Stellar Blade and Lies of P keep the defaults (`ASYNC=1`). From the 0.30 add-on both of these are
automatic (`DLSS5_PRE_UPSCALE_ASYNC=auto`, `DLSS5_STRENGTH=auto`: per-title table). The strength table gives Cyberpunk 2077
luminance-only transfer (`1,0`): the pre-upscale route hands the network the colour buffer before the game's tone mapper and
LUT, and taking the network's hue there turned green neon ambient brown; luminance-only keeps the detail gain (+19% vs +22%
high-pass energy over plain FSR, measured 2026-09-24) with the game's own colours. Forza Horizon 6 (Xbox app build, tested
2026-09-25): the regular package loads, but like Wo Long 2 the game keeps drawing after the upscaler in the same command list,
so the pre-upscale route gives up on the first frame and passes everything through (F6 does nothing; `native-pre-upscale.txt`
says `UNSAFE: draw/dispatch after deferred upscaler`). Set `DLSS5_PRE_UPSCALE=0` in `native-game-flags.txt` to take the
post-upscale route instead: 2K quality (1508×848 render) about 44 fps, F6 works; the yellow fps overlay does not show. The
next release detects this automatically.

**Where this came from.** 0.15 and earlier ran the network as Direct3D 12 Shader Model 6.10 wave-matrix shaders (now in
`shaders/dx12-network/`, still the bit-exact reference chain). 0.20 moved inference to HIP with bit-identical output and
≈8% less time; 0.22 padded the 900 tier to 960 rows; 0.24 introduced the pre-upscale (render-resolution) path; 0.26.1–0.28.1
built the RE9 host/runtime route with TheAutomatic. Details per version are in the changelog.

## What is in this repository

| Directory | Content |
|---|---|
| `src/` | Standard ReShade add-on, image codecs and game integration; shared by Magpie and regular OptiScaler packages. |
| `hip/` | Shared HIP neural kernels and gfx1200/gfx1201 build recipes for all three packages. |
| `shaders/` | Twelve D3D12 glue shaders every current package still compiles at runtime (codec encode/decode, text overlay, RGB staging, temporal coordinates, frame checks); `shaders/dx12-network/` holds the historical Shader Model 6.10 network chain (0.15 and earlier). See `shaders/README.md`. |
| `scripts/` | Add-on builds, package instructions and tracked defaults: `hip-game-flags.txt`, `hip-magpie-flags.txt`, and the new RE9 host overlay `re9-presr.ini`. |
| `Development/HIP/` | HIP host and D3D12 interop code, validation and experiments. Some files here are build dependencies; experiments are not automatically production changes. |
| `Development/RE9/presr/` | RE9-specific host/runtime adaptation: pinned upstream revision, patches, preparation/build/install scripts and evidence. The complete upstream host tree is not vendored here. |
| `Development/tools/` | Full-package assembly using verified framework baselines, model assets and matched modules. |
| `Development/results/`, `Development/deployments/` | Regression/performance evidence and deployment inventories. |
| `Development/DevHistory.md` | Completed development work; plans and limitations live in the respective topic documents. |
| `tools/` | Output comparisons and image statistics. |

**Source and complete distributions are managed separately.** Git contains our add-on/runtime/HIP sources and RE9 host patches. It does not contain the complete Magpie or regular OptiScaler framework source, or all model/framework assets needed to reproduce the full ZIPs. The RE9 host derives from [TheAutomatic's release/1.9.0](https://github.com/TheAutomatic/dlss-5-amd-project/tree/release/1.9.0); `upstream.json` pins its revision and `prepare-host.py` applies adaptations. Its GPL license is retained separately from this project's MIT code.

On the maintainer's machines, Linux holds this repository; Windows on the RX9070 system holds framework baselines, models and built artifacts. Complete releases are under `D:\給網友打包`; the RE9 host source/output are under `D:\DLSSNR-Lab\re9-presr\source` and `bin`, and HIP experiment artifacts under `D:\DLSSNR-Lab\hip-backend`. These are maintainer paths, not required user installation directories. New releases start from verified ZIPs, not a live game installation.

Integration: **Magpie / regular OptiScaler → dlss5-amd.addon64 → shared HIP kernels**; **RE9-specific OptiScaler → LmxxfNrRuntime.dll → the same HIP kernels**. The RE9 host and runtime must be used as a matched pair.

### Optional Runtime recovery for D3D12 hosts

The standalone `LmxxfNrRuntime.dll` C API in `include/LmxxfNrApi.h` can recover a frame when HIP enqueue fails or the submitted command queue differs from the session queue. Set `LmxxfNrCreateInfo.flags |= LMXXF_NR_CREATE_FLAG_ZERO_OUTPUT_FALLBACK` before `Create`. The default remains strict error reporting.

With this flag, `EnqueueHip` returns `LMXXF_NR_OK` after a verified zero clear of its private neural output; the normal decoder view then uses the original Color. Debug views retain their own display behavior. `GetLastError` contains a recovery diagnostic for that call. The application must submit input and output work on the queue passed to `EnqueueHip`, and submit output-reading work before `Retire`, `Drain`, or `Destroy`. The Runtime waits for the supplied queue before it reuses or frees the output. It cannot discover output readers on other queues; the application must synchronize those queues before reuse or destruction. If clearing fails or completion is uncertain, `EnqueueHip` returns `LMXXF_NR_FAILED`; do not submit output-reading work or reuse the session.

Direct C++ Bridge users may call `D3D12Bridge::ClearOutput(queue)` after submitting the producer and before submitting the consumer. They must drain prior users of the output on other queues before calling it, then drain a different consumer queue before bridge reuse or destruction. The Bridge creates D3D12 zero-clear resources only if this fallback is used. No model weights, shader assets, or HIP modules are changed by this feature.

The C host smoke test is `tools/lmxxf_zero_fallback_abi.c`. On Windows, build the Runtime with `scripts/build-runtime.cmd bin`, then compile the test with `gcc -std=c11 -Wall -Wextra -Werror -I include tools/lmxxf_zero_fallback_abi.c -o bin/lmxxf_zero_fallback_abi.exe` and run it from `bin/`. This checks the exported API table and flag handling without a GPU.

## How it works, briefly

- **Network**: pre-block (C32 @1920×1152) → encoder (C32 ×4, C64 ×4, C128 ×6, C256 ×8, C512 ×8) → 8 global ViT blocks
  (640 tokens × 1024) → decoder (C512 → C32, skip connections) → post-block 70 → RGB head. Window attention on 8×8
  windows, FFN with a 4× hidden layer, the residual stream carried as E4M3 bytes between blocks.
- **Kernels** (`hip/`): every GEMM is an RDNA 4 WMMA 16×16×16 instruction with FP8 (E4M3) or f16 operands and an f32
  accumulator, operands loaded straight from memory or LDS; row reductions (normalisation sums, softmax denominators) are
  WMMAs against an all-ones tile; quantisation uses the hardware FP8 casts. The C32 block runs FFN + attention + projection
  of one 8×8 window in a single 128-thread group with the hidden activations kept in registers; the C64–C256 attention keeps
  its exponentials in registers; weights are prepacked into WMMA fragment order at initialisation. 29 modules per architecture (from 0.31).
- **Game side**: the host hands us the frame at render resolution; the codec (`shaders/native_codec_encode.hlsl`) encodes it onto
  the network surface (fitting any input size to the 720/900/1080 tier, reflected padding), the network runs on a D3D12↔HIP
  shared buffer with fences, and the decode shader composes the result with the original frame (the network steers luminance
  and colour of the full-resolution picture) before the host upscaler sees it. In the pre-upscale (render-resolution) path
  the network's own temporal history is reset every frame and the upscaler does the temporal work; the Magpie path has no
  game motion vectors. An overlay shows the state (`DLSS5 ON 1707x961 -> FSR 2560x1440`, INITIALIZING, UNSUPPORTED).
- **Numerics**: an "exact" reference chain reproduces NVIDIA's kernels bit for bit (explicit f16 rounding at every step); the
  shipped fast chain relaxes that (f32 accumulation, hardware rounding) and is judged against it (≈42 dB PSNR). Every kernel
  change since 0.20 is bit-exact against the previous version unless its flag says otherwise (only `HIP_FFN_WAVE_NORM`, off
  by default), verified by a 12-frame RGB hash regression before each candidate is installed.

## Building

### Building the current packages (0.32) — where every shipped file comes from

| Shipped file | Source | Build |
|---|---|---|
| `dlss5-amd.addon64` (Magpie / OptiScaler packages) | `src/native_submission_order_probe.cpp` + `src/*.h`, `Development/HIP/*.h` (bridge) | `bash scripts/build-addon-oneclick.sh dlss5-amd.addon64 --hip` on Linux/WSL (fetches MinHook + ReShade 6.8 headers into `third_party/`; needs `g++-mingw-w64-x86-64`). The build is not byte-reproducible; judge a rebuild by behaviour, not hash |
| `DLSS5-AMD\native-game-tiled-assets\HIP\gfx1200\*.hsaco`, `...\gfx1201\*.hsaco` (29 modules each from 0.31) | `hip/*.hip`, `hip/wave_owned_*.inc`, recipe `hip/build-modules.ps1` | on any Windows box with an AMD driver that ships `amd_comgr_3.dll`: `x86_64-w64-mingw32-g++ -std=c++17 -O2 -static hip/rtc_compile.cpp -o rtc_compile.exe`, then `powershell -File hip\build-modules.ps1 -Compiler rtc_compile.exe -OutputDir <out>` (both targets by default; `-Only <name>` for one module). No GPU is needed to compile; the assembly lands next to each `.hsaco` as `.hsaco.s`. About 6 min for both targets. Every build embeds a random `__hip_cuid_…` symbol, so compare a rebuild with `python3 hip/compare-modules.py <out> <package>\DLSS5-AMD\native-game-tiled-assets\HIP` (code sections), not by file hash; checked 2026-09-26 on a fresh clone: 52 of 58 identical, the other 6 are the non-packed fallbacks `c32_fused_ffn_attention` / `deep_fast` / `multihead-fast-padded-wave` (not loaded by default) that packages still carry from an older build |
| the twelve `shaders/*.hlsl` (codec encode/decode, text overlay, RGB staging, temporal coordinates, frame checks) | `shaders/` (the historical DX12 network chain lives in `shaders/dx12-network/`) | copied as source; compiled at runtime by the system `d3dcompiler` (44 variants selected by `#define`s from the host) |
| RE9 package: `dxgi.dll` (modified OptiScaler host) + `LmxxfNrRuntime.dll` | TheAutomatic's fork `release/1.9.0` @ `8f71f73` + our patches in `Development/RE9/presr/` | `python3 Development/RE9/presr/prepare-host.py` (needs the pinned host checkout: `git clone https://github.com/TheAutomatic/dlss-5-amd-project /tmp/re9-upstream-bridge-review && git -C /tmp/re9-upstream-bridge-review checkout 8f71f73`; rewrites the host/runtime sources and copies `src/`, `shaders/`, `hip/` into `third_party/lmxxf/`), then `bash Development/RE9/presr/build-runtime.sh` (MinGW, runtime + smoke test) and `build-host.ps1` on Windows (Visual Studio 2022 Build Tools, MSVC v143 + Windows SDK 10.0.26100; found through vswhere or `-MSBuild <path>`). Without Linux: every RE9 package carries the prepared sources as `sources\re9-presr-source.tar.gz`, so `powershell -File Development\RE9\presr\build-host.ps1 -Root <work dir> -Archive <that tar.gz>` builds the host (`<work dir>\bin\OptiScaler.dll`, shipped as `dxgi.dll`). The upstream checkout can also sit elsewhere: `RE9_UPSTREAM=<dir>` — see `Development/RE9/presr/README.md`; the same sources are shipped as `sources/re9-presr-source.tar.gz` (`bundle-source.py`) |
| standalone `LmxxfNrRuntime.dll` (API in `include/LmxxfNrApi.h`, contributed by TheAutomatic) | `src/LmxxfNrRuntime.cpp` | `bash scripts/build-runtime.sh` (Linux/WSL MinGW), or `scripts\build-runtime.cmd` on Windows (MSYS2 UCRT64 g++, `pacman -S mingw-w64-ucrt-x86_64-gcc`; set `LMXXF_GXX` to use another g++) |
| `DLSS5-AMD\native-game-flags.txt` (a package also honours `DLSS5_HIP_MODULES=<dir>` to load modules from elsewhere; `Development/HIP/validate-modules.ps1` runs the bit-exact checks on a module set) | `scripts/hip-game-flags.txt` / `hip-magpie-flags.txt` / `hip-re9-flags.txt` (documented in `scripts/CONFIGURATION.md`) | copied |
| weights (`*.f16` / `*.f32`), `noise.f32` | not in this repository (see *Weights*) | packages carry them; a fresh package is built from the previous full package |

Packaging: `Development/tools/package-032.ps1` (Windows) unzips the previous full packages, verifies every file against their `SHA256SUMS.txt`, swaps in the changed files listed above (each hash-checked, the modules additionally against the copies installed on the test machine), compiles the fit shaders, writes `release.json`, `SHA256SUMS.txt` and the zip, and reads the zip back. Earlier versions: `package-031.ps1` … `package-026.ps1`, `package-0281-re9.ps1`.

Validation before shipping kernels: `Development/HIP/validate-modules.ps1` (bit-exact checks of a module set against goldens) and the whole-network regression used for every production candidate (`Development/deployments/stellar-prod6-20260923/regression-prod6.ps1`: 12-frame RGB hashes on two input sequences, 1000-frame timing, extra controls) — every kernel change in this repository since 0.20 is bit-exact with the previous one unless its flag says otherwise (`HIP_FFN_WAVE_NORM`, off by default, is the only non-bit-exact switch).

How the kernel work is organised (for contributors): every optimisation is an experiment under `Development/HIP/experiments/<name>/` (a `prepare.py` that patches the production source into `_pairN` variants or module sets, `build.ps1`, `run.ps1`, sometimes `analyze.py`), with its result written up in `Development/results/<name>-<date>/README.md`; adopted changes become a `HIP_*` flag with the default set in the source. `Development/WorkingPlan.md` says what is being worked on; `Development/DevHistory.md` records what was done and why.

### Historical: DX12 editions (up to 0.15)

**None of the following is needed for the current packages** (HIP backend since 0.20: no preview DXC, no Agility SDK, no developer mode). It is kept for the DX12 wave-matrix implementation in `shaders/`, which remains the bit-exact reference chain and the history of the port.


Requirements: Linux / WSL with `x86_64-w64-mingw32-g++` (cross build), Windows with an RDNA 4 GPU and a driver exposing
D3D12 wave matrices (linalg tier 10), the Shader Model 6.10 preview `dxc` (with `dx/linalg.h`), ReShade 6.8 add-on
headers, MinHook sources.

Where the preview pieces come from (all linked from Microsoft's post
[Announcing Agility SDK 1.721 preview and more Shader Model 6.10 features](https://devblogs.microsoft.com/directx/announcing-agilitysdk-721-preview-and-more-shader-model-6-10-features/)):
the preview DXC is a *preview* release of [microsoft/DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler/releases)
(we use v1.10.2605.24, `dxc_preview_2026_05_22.zip`; unzip anywhere and pass the folder as `-DxcRoot`); the Agility SDK
runtime (`D3D12Core.dll`, folder `DLSS5-D3D12-721` in the package) is NuGet `Microsoft.Direct3D.D3D12` 1.721.3-preview;
the AMD driver is the RC "Agility SDK" build 26.10.07.02 (32.0.31007.2048), not a release driver: [download from AMD](https://drivers.amd.com/drivers/amd-software-adrenalin-edition-26.10.07.02-win11-rc7-agility-sdk.exe).
**Windows Developer Mode must be on** (Settings → System → For developers): the add-on enables the experimental shader models with
`D3D12EnableExperimentalFeatures`, which only succeeds in developer mode; without it the initialisation stops at its first step
(`sdk721_before_device ... experimental=` in `logs\native-submission-order.txt` is not `00000000`). Nothing else has to be installed
to run a release package: the shaders are precompiled.

```bash
# one click (Ubuntu / WSL): sudo apt install g++-mingw-w64-x86-64 git; fetches MinHook + ReShade headers into third_party/
bash scripts/build-addon-oneclick.sh            # -> native-game.addon64
# or by hand
bash scripts/build-addon.sh <minhook-src> <reshade-include> native-game.addon64 --tiled
bash scripts/build-bench.sh native-network70-temporal.exe
```

```powershell
# shaders only, any Windows x64 machine (needs the SM 6.10 preview dxc package; no GPU, no weights)
powershell -ExecutionPolicy Bypass -File scripts\compile-shaders.ps1 -Folder D:\dlss5-shaders -DxcRoot <dxc-preview>
# shaders + bench on the RX 9070 XT machine: <lab> holds the shaders, the weights and the bench exe
powershell -ExecutionPolicy Bypass -File scripts\bench.ps1 -Folder <lab> -DxcRoot <dxc-preview>
powershell -ExecutionPolicy Bypass -File scripts\deploy_fast.ps1 -Source <lab> -Dll native-game.addon64 -Flags scripts\game-flags.txt
```

## Changelog

Unless a Magpie scenario is specified, frame rates are Stellar Blade at 1920×1080 on an RX 9070 XT; "bench" is the offline test bench (network only). Every tag
after 0.01 keeps the bit-exact reference chain as its judge (≈ 42 dB PSNR against it); "bit-exact" below means the fast
chain's own output did not change by a single bit.

| Version | Date | What changed | Result |
|---|---|---|---|
| `0.01` | 09-08 | End of the exact port: all 71 blocks on wave-matrix kernels, bit-for-bit equal to the original network for 15 frames; weights resident in VRAM (no per-frame PCIe traffic); scratch shared between blocks (14.7 → 7.3 GB) | bench 186 ms, ~5 fps in game |
| `0.02` | 09-08 | Fast chain begins (exact chain frozen as the judge): FP32 hardware accumulation, E4M3 operands, activation epilogue and attention without intermediate f16 roundings; temporal path (motion vectors + previous output) hooked up in game | bench 112 ms, ~8 fps |
| `0.03` | 09-08 | Hardware f16/E4M3 conversions, fused QKV+normalize, C32 attention in two dispatches, ViT packed inputs, C512 direct attention, FP8 residual stream in the multi-head blocks | bench 62.7 ms, ~15 fps |
| `0.04` | 09-09 | Deferred submission ring, command-list batching (~100 → ~25 lists/frame), C32 attention with QKV folded in, noise prefix generated on the ALU instead of a 200 MB table, ViT QKV fused | game GPU 32 ms, 23–25 fps |
| `0.05` | 09-09 | Output-side temporal smoothing (the rain-scene shimmer), C32 attention two windows per group, ViT attention on FP8 | 24–25 fps |
| `0.06` | 09-09 | Repository restructured (`src/ shaders/ scripts/`), `bench.ps1` flattens the 76 nested runners, per-frame GPU probe off by default (its Flush cost 3 ms), pre-block output as E4M3 | 27–28 fps |
| (0.07) | 09-09 | User package only, no tag: three blocks skipped (40.7 dB), VRAM 6.8 → 3.75 GB with periodic MakeResident, C32 intermediates as f16, post-block merge folded into its FFN | 29 fps |
| `0.08` | 09-10 | C32 FFN fused into the attention prologue; tiled (non-power-of-two stride) layouts for ViT / C512 / decoder-entry weights; C512 FFWD output as E4M3 tiles read directly, C512 blocks mapped to the raster with an E4M3 stream between them (no window pack/crop, no QKV pack); post merge 4 channels per load; decoder projection epilogue with coalesced writes — all bit-exact. Rise of the Ronin via XeSS (`xessD3D12Execute` hook). Profiling toolchain (headless RGP capture, ISA statistics). Black-frame probe. Texture quality "High" or lower is now a stated requirement | bench 24.4 ms, 3.1 GB VRAM, 36–37 fps |
| `0.09` | 09-11 | Upsample projections 56/62/66 write f16 rasters read directly by the next block (bit-exact, −0.2 ms). **Magpie edition**: the add-on now loads in any process (the hard-coded exe whitelist that produced ReShade error 1114 in other games is gone), hooks the FSR 4 SDK loader's `ffxDispatch` as well, accepts 8-bit UNORM textures, scales motion vectors by the dispatch's `motionVectorScale`, and arms on a configurable frame (`DLSS5_SNAPSHOT_FRAME`), so it runs inside the SAOG0721 Magpie fork's FSR3 effect: any game in a 1920×1080 borderless window, no FSR/DLSS support needed (~30 fps; 8-bit sRGB input, so the picture is brighter than the in-game hook). ViT expand+contract fusion tried and rejected (2× slower, latency-bound) | bench ≈24.2 ms, 36–37 fps in game; Magpie ≈30 fps |
| `0.10` | 09-11 | Black-block root cause fixed: the hardware E4M3 cast does not saturate, so a residual beyond ±448 became NaN, the NaN token spread over its 8×8 attention window and the rgb head clamped it to 0. The fused C32 block now clamps to ±448 before its hardware casts (`DLSS5_BUILD_C32_SAT_CAST`, FFN input / hidden / attention input / AV output). Bit-identical on the reference fixture; the Magpie dump frames go from 25920 NaN head values to 0. Windows Developer Mode documented as a requirement (`D3D12EnableExperimentalFeatures`). Magpie bundle `Magpie-DLSS5-AMD-0.10.zip` | unchanged |
| `0.11` | 09-11 | Take-over time 20-30 s → ~3.5 s: weight files prefetched into memory when the add-on loads (`NativePrefetchWeights`), the ~1000 resident weight copies batched into one GPU wait instead of one queue+wait per table (`NativeResidentBatch`; the first version released a copy destination early and hung the GPU — both ends are now held until the flush), the six runtime-compiled shaders cached on disk (`shader-cache\`), f16 weight expansion on 8 threads. Numerically unchanged (reference fixture bit-identical). Init timing probes (`DLSS5_VRAM_LOG=1` / `DLSS5_INIT_LOG=<file>`). Two-pass C32 softmax tried and kept off (null: the 896-byte scratch belonged to a PSO that is never dispatched; the fused kernel has none). Magpie bundle `Magpie-DLSS5-AMD-0.11.zip` | unchanged |
| `0.12` | 09-12 | On-screen notice (`native_text_overlay.h` / `.hlsl`): when the upscaler's output is not 1920×1080 (a 2K/4K screen with Magpie set to fit the screen, or a non-1080p game window) the add-on writes "DLSS5-AMD: INPUT MUST BE 1920X1080 (NOW WxH)" into the picture instead of silently watching; "INITIALIZING..." during the 3-5 s take-over and "INIT FAILED - SEE DLSS5-AMD\LOGS" when initialization fails (developer mode off, wrong driver). A 5×7 bitmap font drawn into our own buffer and copied into the host texture on our own command list after the game's batch (a UAV on the host texture or recording into the game's list crashes D3D12Core in Magpie). `DLSS5_NOTICE=0` in the flags file turns it off. Also: `DLSS5_OVERLAP` (network on its own compute queue, one frame behind; null on this GPU, default off) and `DLSS5_BUILD_C32_LDS_SLIM` (fused C32 kernel with 4 KB less LDS; bit-identical, no gain, default off). Numerically unchanged. Magpie bundle `Magpie-DLSS5-AMD-0.12.zip` | unchanged |
| `0.13` | 09-12 | `DLSS5_SHOW_FPS=1` (on in the Magpie bundle): the network's own frame rate drawn in the corner with the notice font. The hook now takes over the upscaler output in whatever state the game declares for it (mapped ffx_api state bits), not only UAV. Pre-block errors carry the source line. Found the hard way: Windows Update silently replaces the preview driver with the release one (SM 6.10 gone, every PSO fails with E_INVALIDARG, the screen says INIT FAILED) -- reinstall 26.10.07.02 and set `ExcludeWUDriversInQualityUpdate=1`. The Magpie bundle's effect group now has XeSS Frame Generation (ZeroMV, vendor-agnostic) after FSR3_SR: 28 real → 55 presented fps on the 9070 XT, one frame of latency, the network itself ~20% slower next to the optical flow. Numerically unchanged. Magpie bundle `Magpie-DLSS5-AMD-0.13.zip` (sha256 9104C48D…) | unchanged |
| `0.14` (bundle) | 09-12 | FPS display: update at intervals of at least three seconds, reuse the unchanged text strip, and copy it at the end of the existing output submission instead of a separate synchronous submission. XeSS FG ZeroMV remains enabled in the preset. Remove backup DLLs, logs and shader caches from the bundle; regenerate file checksums. `Magpie-DLSS5-AMD-0.14.zip`, 358,004,639 bytes; SHA256 `14ccde3c752b40821cb9f30024579304627a2499e087e06e9aec399bfe734eed`. No 0.14 tag yet. | Build passed; 671 archive files verified; FPS gain not measured |
| [0.15](https://pan.quark.cn/s/1601ca8f80ae) | 09-13 | Accept ordinary windows with width ≤1920 and height ≤1080; fit smaller inputs to the fixed network surface while preserving aspect ratio, then restore the source extent before FSR4 fills the screen. Portable preset: FSR3 (the DLSS5 entry point) → FSR4 fill screen → XeSS FG ZeroMV. `DLSS5_FIT_INPUT=1`; retain the three-second FPS display. Bundle `Magpie-DLSS5-AMD-0.15.zip`.  358,010,545 bytes; SHA256 `9bb7a021d09d987986f96dbd920589018606c9800dbd08e007f4b309388e5909`.| 672 archive files verified; Onimusha Medium at 2K ~30 fps |
| [0.15-900P](https://pan.quark.cn/s/a5339e4c8549) | 09-14 | Fixed 1600×900 inference (1600×1024 processing, 400 ViT tokens), followed by FSR4 scaling; AMD optical flow enabled only on the DLSS5/FSR3 item. User reports better image quality than 720p and steadier frame rates than 1080p with FG. Isolated inference is about 16.71 ms, not game frame time. Bundle `Magpie-DLSS5-AMD-0.15-900P.zip`, 358,038,890 bytes; SHA256 `718d77941674d6e851e7babc14b40a596da52e86aec603f44f956b99ff6811d5`. | 900p gameplay tested by user; 676 archive files verified |
| [0.20](https://pan.quark.cn/s/3c8b5329353c) (HIP) | 09-17 | HIP backend: COMGR-compiled gfx1201 kernels (sources and build recipe in `hip/`, experiments in `Development/HIP/`), D3D12↔HIP shared buffers/fences, bit-exact with the DX12 chain. Kernel-level work of 09-16/17 (ISA-driven: branchless conversions, batched loads, hoisted reloads, prepacked residual diagonals, inlined prefix) brought the isolated 900p frame from 19.4 to ≈15.5 ms, 8% faster than the DX12 chain; in-game 900p 52 fps. Packages `DLSS5-AMD-0.20.zip` (game) and `Magpie-DLSS5-AMD-0.20.zip` (Magpie, no Agility runtime). Requires `amdhip64_7.dll` from the AMD driver. |
| [0.21](https://pan.quark.cn/s/85a507a744bd) (HIP) | 09-17 | `DLSS5_NETWORK_HEIGHT=auto`: network tier chosen from the input window (≤1280×720 → 720, ≤1600×900 → 900, else 1080), the FPS overlay shows the tier (`1600X900`); package notes: measured 1.2 GB VRAM, release driver reported working, performance tier (`DLSS5_SKIP_BLOCKS` nine-block set, −0.8 ms at ≈30 dB). Kernels, weights and output identical to 0.20. In play: 900p 52–54 fps, 1080p 37–38 fps. Packages `DLSS5-AMD-0.21.zip` (sha256 38e15189…) and `Magpie-DLSS5-AMD-0.21.zip` (sha256 8eeee2dd…). |
| [0.22](https://pan.quark.cn/s/03f9995d0551) (HIP) | 09-17 | 900 tier padded to 960 rows instead of 1024 (60 reflected rows + one zero-token row, the 1080 tier's scheme): same kernels, −0.9 ms (6%, 15.8 → 14.9 ms same batch; ≈14.4 ms on the quiet machine), 900p ≈ +3 fps. Output differs from 0.21 (31.5 dB, both layouts are self-defined); `DLSS5_NETWORK_HEIGHT=900w` restores the 0.21 layout. New 960-row goldens (`validate-modules-960.ps1`). Packages `DLSS5-AMD-0.22.zip` (sha256 59226956…) and `Magpie-DLSS5-AMD-0.22.zip` (sha256 8186b67d…). |
| 0.23 · [Magpie](https://pan.quark.cn/s/548e52cc4f49) · [OptiScaler](https://pan.quark.cn/s/8b5a402012a2) (HIP) | 09-18 | Fix for hosts with an iGPU (AMD Radeon(TM) Graphics) or a second GPU: initialization failed with `bridge currently requires exactly one HIP GPU` (INIT FAILED on screen, user report 2026-09-18). The bridge now enumerates the HIP devices and picks the one whose name matches the game's D3D12 adapter (first match; two identical cards would still pick the first). Single-GPU hosts are unchanged; kernels, weights and output identical to 0.22 (Stellar Blade 1080p 37 fps, 900p 52 fps re-checked). Packages `DLSS5-AMD-0.23.zip` (sha256 3dde0e0a…) and `Magpie-DLSS5-AMD-0.23.zip` (sha256 7146569c…). Added 09-19: `OptiScaler-DLSS5-AMD-0.23.zip` (sha256 9d70d28f…), bundling OptiScaler 0.9.4 + ReShade + the same 0.23 HIP add-on. Tested in Stellar Blade with FSR inputs and both FSR 2.1 and FSR 3.x/4 backends; defaults to FSR 3.x/4, frame generation off. Other games remain unverified. |
| 0.24 · [OptiScaler](https://pan.quark.cn/s/1f32ffbd2e96) (HIP) | 09-19 | Pre-upscale path: low-resolution game color → DLSS5 → OptiScaler FSR 3.x/4 → display output. Changes our add-on, not OptiScaler or the network kernels/weights. Same-queue asynchronous submission; tested in Stellar Blade at 2560×1440, FSR Quality (1707×961 input), about 34–35 fps in play. DLSS5 history is reset every frame for now; FSR temporal processing remains enabled. Network input must still fit within 1920×1080; 4K output and Magpie regression are not yet verified. Package `OptiScaler-DLSS5-AMD-0.24.zip` (385,880,495 bytes; sha256 2a46adeb…). |
| 0.24.1 · [OptiScaler](https://pan.quark.cn/s/4f73a54d0ff9) (HIP) | 09-19 | Asset/config lookup fix: when `DLSS5-AMD` is absent beside a relocated add-on DLL (for example under `_storage_`), also check beside the game executable before falling back to the development directory. Rechecked the pre-upscale chain in Stellar Blade at 2560×1440; network kernels and weights unchanged. This does not resolve Resident Evil Requiem’s same-command-list compatibility issue. Package `OptiScaler-DLSS5-AMD-0.24.1.zip` (385,883,920 bytes; sha256 1e9ec721…). |
| 0.24.2 · [OptiScaler](https://pan.quark.cn/s/f74aaa5c7f9a) (HIP) | 09-19 | Skip idle per-draw configuration lookups and job-map locking, and avoid log counter contention after the quota. F6 off bypasses new pre-upscale captures/color copies while draining already captured work once. Stellar Blade city test recovered to about 49 fps (previously about 31); synchronous/asynchronous GPU checks passed. Kernels and weights unchanged. Package `OptiScaler-DLSS5-AMD-0.24.2.zip`. Full package rebuilt the same day to fix FPS/status visibility settings; download link updated. |
| 0.25 · [Magpie](https://pan.quark.cn/s/09630ed99606) · [OptiScaler](https://pan.quark.cn/s/636691131c5f) (HIP) | 09-19 | **Optimizations**: reuse C32/multihead attention exponentials and simplify reciprocal calculation; pass intermediate features and decoder outputs as FP8 bytes to reduce data movement; use a fast path for full decoder tiles. **Fixes/support**: fix missing 900-tier tail writes and Unicode-path loading; add gfx1200 kernels for RX 9060/XT with automatic gfx1200/gfx1201 selection. RX 9070 XT regression passed; RX 9060/XT awaits hardware feedback. Magpie at 1080p with DLSS5 only measured about 37 fps, roughly unchanged. Full packages available for both Magpie and OptiScaler. |
| 0.26 · [Magpie](https://pan.quark.cn/s/7ce2ca11db43) · [OptiScaler](https://pan.quark.cn/s/c880a70f0824) (HIP) | 09-19 | FFN reads FP8 byte fragments directly, removing input staging and two barriers; C256 weights are prepacked into contiguous matrix fragments at initialization to reduce scattered loads and byte assembly. Include the missing R11G11B10 decoder shader to fix black output at lower Effects Quality in Lies of P, confirmed by user testing; add shader compilation/binding checks. Both full packages built; archive contents and 44 shader variants verified. |
| 0.26.1 · [OptiScaler-REFramework](https://pan.quark.cn/s/624c87a6aa11) (HIP, special-purpose build) | 09-20 | **For unusual integration cases such as RE9; not the standard release. Use the general package for ordinary games. Only Resident Evil Requiem has been tested.** Post-FSR HIP compatibility with R10G10B10A2/FP16 conversion, a fixed 900p network and a 1080p SDR output limit. Status, resolution and Present FPS overlay with F7 visibility control. Includes REFramework, OptiScaler, ReShade, complete model assets and gfx1200/gfx1201 kernels; retains 0.26 optimizations. User gameplay validation passed. |
| 0.27 · [Magpie](https://pan.quark.cn/s/ec3a3282aa76) · [OptiScaler](https://pan.quark.cn/s/004278159ed8) · [OptiScaler-REFramework](https://pan.quark.cn/s/010683548f68) (HIP) | 09-20 | Exact streaming ViT attention reduces intermediate storage and repeated reads. Optional R3 adaptive reuse adds change checks, identical-input cache extension and fused anchor updates; disabled by default. Defaults are copied from tracked per-variant templates. Full model/runtime payloads and gfx1200/gfx1201 modules; no INT4 or pruning. REFramework retains fixed 900p / max1080p SDR post-processing. |
| 0.28 · [Magpie](https://pan.quark.cn/s/11547f398eb4) · [OptiScaler](https://pan.quark.cn/s/f7f423b0ea3a) (HIP) | 09-22 | Six lossless kernel improvements: RGB read sharing, C128/C256 zero-padding shortcuts, fixed-shape ViT expansion/projection and decoder projection. Stellar Blade gameplay/FPS essentially unchanged. Regular hosts unchanged; full model and dual-architecture kernels included. RE9 0.28 has been withdrawn; use 0.28.1 below. |
| 0.28.1 · [OptiScaler-REFramework](https://pan.quark.cn/s/1375693a0d21) (HIP) | 09-22 | RE9-specific full package: reject oversized render input before HIP initialization, keep original SR, roll back failed initialization and recover valid sizes; protect unretired frames. Ten regression cases/twelve submitted frames and initial user testing passed. Update the matched host/runtime together; corresponding source and TheAutomatic credit included. |
| 0.29 · [Magpie](https://pan.quark.cn/s/fe1b6af36cad) · [OptiScaler](https://pan.quark.cn/s/209e04e7acaf) · [OptiScaler-REFramework](https://pan.quark.cn/s/505d38a63a85) (HIP) · all three on [Google Drive](https://drive.google.com/drive/folders/1VPsX33sLTxBG4J8kJ_IzBDlkbBTCc5Eo?usp=sharing) | 09-23 | Inputs above 1920×1080 are fitted onto the 1080 tier and composed back at source resolution (`DLSS5_FIT_LARGE=1`, issue #6; Stellar Blade 2K Native AA 44 fps, RE9 Native AA tested; ultrawide untested). Six bit-exact kernel improvements since 0.28 (fence scope, folded C32 FFN, byte chain + vectorised staging, register-resident attention, in16 aliasing, transposed FFN tail): about −7%, Stellar Blade 900p ~60 fps. RE9 host unchanged, runtime updated. |
| 0.30 · all three packages (Magpie · OptiScaler · OptiScaler-REFramework, HIP) on [Quark](https://pan.quark.cn/s/80a735ab9f88) and on [Google Drive](https://drive.google.com/drive/folders/1pKZpLosgJXxUOZTMg_m0sbCipX9Q3WYo?usp=sharing) | 09-25 | Chain launches any-order with per-tile flags (programmatic-dependent-launch emulation, `DLSS5_HIP_PDL=1`; bit-exact; 900p about −1.6%, 1080p −0.6%; `results/pdl-chain-20260925`) and FFN full-line stores (bit-exact, −0.6%). Cyberpunk 2077 zero-config: regular `OptiScaler.ini` `[Inputs] EnableFfxInputs=false`, `DLSS5_PRE_UPSCALE_ASYNC=auto` (2077 synchronous: transient aliased colour buffer), `DLSS5_STRENGTH=auto` (2077 luminance-only 1,0: hue taken before the game LUT turned green ambient brown). Fix: preset/resolution switch killed neural processing (FSR context pin never cleared through the shim; restart budget now counts failures only). RE9 package: same kernels, host/runtime unchanged from 0.29. Stellar Blade 900p→2K 60–61 fps, 1080p→2K 47–48; Cyberpunk low balanced 51–52, quality 41. |
| 0.31 · all three packages (Magpie · OptiScaler · OptiScaler-REFramework, HIP) on [Quark](https://pan.quark.cn/s/e76b8611e3cc) · [Google Drive mirror](https://drive.google.com/drive/folders/1xtBe_XhgF9eqBlrlIQMgWcEkzm0UKHIZ?usp=sharing) | 09-26 | Tier choice: an input at most 110% of a tier on both axes is fitted down into it (2K quality 1707×961 → 900, previously upscaled into 1080). One-head-per-wave kernels (C32/C64/C128 blocks + C256 attention; `DLSS5_HIP_WAVE_OWNED=1`; bit-exact, about −6%); C512 QKV/mix 32 tokens (`DLSS5_HIP_C512_M32=1`, bit-exact, about −1.3%); ViT projection 64 columns (`DLSS5_HIP_VIT_PROJ_N64=1`, bit-exact, about −1.8% at 1080). Regular package defaults `DLSS5_VIT_ADAPTIVE=1` (+3 fps when still, off in motion). 29 modules per architecture. RE9 package: host/runtime as 0.30, new kernels bundled but unused. Stellar Blade main menu 2K quality 57, 2K Native AA 43–44. |
| 0.32 · all three packages (Magpie · OptiScaler · OptiScaler-REFramework, HIP) on [Quark](https://pan.quark.cn/s/b805e071405c) · [Gofile mirror](https://gofile.io/d/CZ67LYIc) | 09-26 | Shared-buffer pool: the driver never returns imported D3D12 shared buffers, now reused per tier (40 switches +3 GB → flat; `results/vram-leak-20260926`). C32 vector input reads (bit-exact, −0.8/−0.9%). RE9 runtime reads flags (`DLSS5_HIP_*`/`SKIP_BLOCKS`/`FIT_LARGE`/`NETWORK_HEIGHT`), 0.31 kernels on by default, compatible with the old host's two-argument `EnqueueHip` (`results/re9-runtime-flags-20260926`); PR #9 merged. RE9 medium 2K Quality 54, native AA 38. |

## Weights

The network weights are NVIDIA's. The runtime weight files the host code loads (`block31-expand.f32`,
`post70-attention.f32`, … about 16 GB with the reference dumps) are not in this repository; they were extracted from a
locally owned copy of `nvngx_dlssnr.dll` with the scripts in `Development/` (`prepare_native_*_gpu.py` and the
per-block notes), which document the layouts but are not a polished pipeline. No NVIDIA DLL is distributed here.

## Authors

Kien — direction, game integration, testing. The reverse engineering, kernels and optimization were
written with AI collaborators (Claude, GPT); the working notes in `Development/` are theirs. Write-ups (Chinese): [WeChat, DLSS5 series](https://mp.weixin.qq.com/mp/appmsgalbum?__biz=MzYzMzMwNzk0NA==&action=getalbum&album_id=4687269655390453762#wechat_redirect). Resume / 简历: [here](https://github.com/lmxxf/ai-theorys-study/blob/main/resume/README.md).

## License

Code in this repository is released under the MIT License. NVIDIA binaries and weights are not covered by it.
