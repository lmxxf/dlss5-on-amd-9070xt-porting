# DLSS 5 (DLSSNR) on AMD RX 9070 XT

[中文说明](README.zh-CN.md)

A from-scratch Direct3D 12 re-implementation of NVIDIA's DLSS 5 neural renderer ("DLSSNR", the 71-block
Swin/ViT network shipped in `nvngx_dlssnr.dll`) that runs on an AMD RDNA 4 GPU. The network was reverse-engineered
block by block, re-written as HLSL compute shaders using Shader Model 6.10 wave-matrix (`dx::linalg`) intrinsics with
FP8 (E4M3) operands, and wired into a game through a ReShade add-on that hooks the FSR dispatch and post-processes the
1080p frame.

**Status (2026-09-10, tag `0.08`)**: Stellar Blade at 1920×1080 runs at **36–37 fps** on an RX 9070 XT with the full
network in the loop (bench: 24.4 ms per frame for the network alone, 3.1 GB of VRAM). Image output matches the bit-exact
reference chain at ≈ 42 dB PSNR. Texture quality must be "High" or lower: at "Very High" the game plus the network
exceed 16 GB and the frame rate collapses. This is a research project, not a product: no tuning UI, one game, one
resolution. `scripts/game-flags.txt` is the exact runtime flag set of this tag; `scripts/bench.ps1` compiles the
matching shader set.

## What is in this repository

| Directory | Content |
|---|---|
| `src/` | Host code: the ReShade add-on (`native_submission_order_probe.cpp` + `native_game_*.h`) and the offline bench (`d3d12_native_network70_test.cpp`); one header per network stage (`native_c64.h`, `native_preblock_runtime.h`, `native_vit_*.h`, `native_post70.h`, …). |
| `shaders/` | The HLSL compute kernels of the fast chain. Wave-matrix kernels are `native_wave_*.hlsl`; the `NATIVE_*` defines select the fast paths. |
| `scripts/` | `build-addon.sh` (mingw-w64 cross build of the add-on), `build-bench.sh`, `bench.ps1` (compiles every shader of the fast chain with the preview `dxc` and runs the bench), `deploy_fast.ps1` / `update-manifest.ps1` (install into the game's asset folder), `game-flags.txt` (the runtime flag set the game currently runs with). |
| `tools/` | `compare_fast_output.py` (PSNR against the exact chain), `flicker_stats.py` (frame-to-frame analysis of the in-game dumps). |
| `Development/` | Everything produced on the way: reverse-engineering notes, per-block reference implementations and validation scripts, the 76 nested experiment runners the fast chain grew out of, plans and state logs. `DevHistory.md` is the single consolidated development history; the original per-period documents are under `history/`. Not needed to build. |

## How it works, briefly

- **Network**: pre-block (C32 @1920×1152) → encoder (C32 ×4, C64 ×4, C128 ×6, C256 ×8, C512 ×8) → 8 global ViT blocks
  (640 tokens × 1024) → decoder (C512 → C32, skip connections) → post-block 70 → RGB head. Window attention on 8×8
  windows, FFN with a 4× hidden layer, f16 residual stream quantized to E4M3 between blocks.
- **Kernels**: every GEMM is a wave-matrix multiply (16×32 A, 32×16 B, f32 accumulator) with E4M3 or f16 operands
  loaded straight from memory; row reductions (normalization, softmax denominators) are MMAs against an all-ones tile;
  quantization uses the hardware `Cast<F8_E4M3FN>`. The C32 attention runs QKV + attention + projection of two windows in
  one 256-thread group.
- **Game side**: the add-on hooks the FSR dispatch, encodes the frame, samples the previous network output through the
  motion vectors (temporal history), runs the network on a deferred submission ring (6 command lists per frame), and
  copies the result back. A small output-side temporal smoothing pass (`native_output_smooth.hlsl`) damps the shimmer
  the network adds around jittering edges.
- **Numerics**: an "exact" chain reproduces the NVIDIA kernels bit for bit (explicit f16 rounding after every step);
  the fast chain relaxes it (f32 accumulation, hardware rounding) and is validated against the exact chain by PSNR.

## Building

Requirements: Linux / WSL with `x86_64-w64-mingw32-g++` (cross build), Windows with an RDNA 4 GPU and a driver exposing
D3D12 wave matrices (linalg tier 10), the Shader Model 6.10 preview `dxc` (with `dx/linalg.h`), ReShade 6.8 add-on
headers, MinHook sources.

Where the preview pieces come from (all linked from Microsoft's post
[Announcing Agility SDK 1.721 preview and more Shader Model 6.10 features](https://devblogs.microsoft.com/directx/announcing-agilitysdk-721-preview-and-more-shader-model-6-10-features/)):
the preview DXC is a *preview* release of [microsoft/DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler/releases)
(we use v1.10.2605.24, `dxc_preview_2026_05_22.zip`; unzip anywhere and pass the folder as `-DxcRoot`); the Agility SDK
runtime (`D3D12Core.dll`, folder `DLSS5-D3D12-721` in the package) is NuGet `Microsoft.Direct3D.D3D12` 1.721.3-preview;
the AMD driver is the RC "Agility SDK" build 26.10.07.02 (32.0.31007.2048) from the same announcement, not a release driver.

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

Frame rates are Stellar Blade at 1920×1080 on an RX 9070 XT; "bench" is the offline test bench (network only). Every tag
after 0.01 keeps the bit-exact reference chain as its judge (≈ 42 dB PSNR against it); "bit-exact" below means the fast
chain's own output did not change by a single bit.

| Tag | Date | What changed | Result |
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

## Weights

The network weights are NVIDIA's. The runtime weight files the host code loads (`block31-expand.f32`,
`post70-attention.f32`, … about 16 GB with the reference dumps) are not in this repository; they were extracted from a
locally owned copy of `nvngx_dlssnr.dll` with the scripts in `Development/` (`prepare_native_*_gpu.py` and the
per-block notes), which document the layouts but are not a polished pipeline. No NVIDIA DLL is distributed here.

## Authors

Kien — direction, game integration, testing. The reverse engineering, kernels and optimization were
written with AI collaborators (Claude, GPT); the working notes in `Development/` are theirs. Write-ups (Chinese): [WeChat, DLSS5 series](https://mp.weixin.qq.com/mp/appmsgalbum?__biz=MzYzMzMwNzk0NA==&action=getalbum&album_id=4687269655390453762#wechat_redirect).

## License

Code in this repository is released under the MIT License. NVIDIA binaries and weights are not covered by it.
