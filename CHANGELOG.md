# Changelog

[中文](CHANGELOG.zh-CN.md) · The changelog table in the README is a one-line summary per release; this file expands each release: what changed, measured effect, switches, whether it is bit-exact, and the matching experiment folder. Oldest first; new releases are appended at the end.

Notes:
- "Bit-exact" means the fast chain's output is identical to the previous release (EXACT and AE). From 0.01 on, the exact reference chain is frozen as the judge; the fast chain is about 42 dB PSNR against it.
- "Offline" is the network-only bench (1000 frames, first 200 dropped). "900 tier / 1080 tier" means the network processes 1600×960 / 1920×1152. In-game numbers are Stellar Blade on an RX 9070 XT unless stated.
- Numbers that were not measured are not written. Experiments live in `Development/results/<folder>/`, the log in `Development/DevHistory.md`.

## Early releases (0.01–0.23)

Details are the matching rows of the README table; key points only.

| Version | Date | Key points |
|---|---|---|
| 0.01 | 09-08 | Exact port done: all 71 blocks on wave matrix kernels, 15 frames bit-identical to the original; bench 186 ms, about 5 fps |
| 0.02 | 09-08 | Fast chain starts (exact chain frozen as the judge): FP32 hardware accumulation, E4M3 operands; temporal inputs wired; 112 ms |
| 0.03 | 09-08 | Hardware f16/E4M3 conversion, fused QKV+norm, etc.; 62.7 ms, about 15 fps |
| 0.04 | 09-09 | Deferred submit ring, command-list batching, ALU noise prefix; 23–25 fps |
| 0.05 | 09-09 | Output temporal smoothing, FP8 ViT attention; 24–25 fps |
| 0.06 | 09-09 | Repo reorganised, GPU probes off by default, E4M3 pre-block output; 27–28 fps |
| (0.07) | 09-09 | Three blocks skipped (lossy, 40.7 dB), VRAM 6.8→3.75 GB; 29 fps |
| 0.08 | 09-10 | Bit-exact layout/direct-read work; Rise of the Ronin XeSS path; bench 24.4 ms, 36–37 fps |
| 0.09 | 09-11 | Upsample projections to f16 raster (bit-exact, −0.2 ms); Magpie build works with any game (about 30 fps) |
| 0.10 | 09-11 | Black-block root cause fixed: clamp to ±448 before E4M3 conversion (`DLSS5_BUILD_C32_SAT_CAST`) |
| 0.11 | 09-11 | Takeover 20–30 s → about 3.5 s (weight prefetch, batched residency, shader disk cache) |
| 0.12 | 09-12 | On-screen notices (wrong resolution / initialising / failed); `DLSS5_NOTICE` |
| 0.13 | 09-12 | `DLSS5_SHOW_FPS`; takeover by declared output state |
| (0.14) | 09-12 | Magpie full package: FPS refresh every 3 s, cleanup |
| 0.15 · [pkg](https://pan.quark.cn/s/1601ca8f80ae) | 09-13 | Windows ≤1920×1080 fitted by aspect (`DLSS5_FIT_INPUT=1`); also [0.15-900P](https://pan.quark.cn/s/a5339e4c8549) |
| 0.20 · [pkg](https://pan.quark.cn/s/3c8b5329353c) | 09-17 | **HIP backend**: COMGR-built gfx1201 kernels, bit-exact with the DX12 chain; 900p about 15.5 ms, in-game 900p 52 fps |
| 0.21 · [pkg](https://pan.quark.cn/s/85a507a744bd) | 09-17 | `DLSS5_NETWORK_HEIGHT=auto` tier selection; bit-exact with 0.20 |
| 0.22 · [pkg](https://pan.quark.cn/s/03f9995d0551) | 09-17 | 900 tier padding 1024→960 rows (−0.9 ms); **output changed** (about 31.5 dB vs 0.21), `900w` restores the old layout |
| 0.23 · [Magpie](https://pan.quark.cn/s/548e52cc4f49) · [OptiScaler](https://pan.quark.cn/s/8b5a402012a2) | 09-18 | HIP device chosen by adapter name on multi-GPU/iGPU machines; bit-exact with 0.22 |

## 0.24 (09-19)

Download: [OptiScaler](https://pan.quark.cn/s/1f32ffbd2e96) (HIP)

- **Changed**: pre-upscale route — the game's low-resolution colour → DLSS5 → OptiScaler's FSR 3.x/4 → final output, asynchronous on the same queue. Only this project's add-on changed; OptiScaler itself, weights and kernels unchanged.
- **Effect**: Stellar Blade 2560×1440, FSR Quality (input 1707×961) about 34–35 fps.
- **Behaviour**: DLSS5 history reset every frame for now (FSR temporal kept); network input still must be ≤1920×1080.
- **Bit-exact**: network output same as 0.23.

## 0.24.1 (09-19)

Download: [OptiScaler](https://pan.quark.cn/s/4f73a54d0ff9) (HIP)

- **Changed**: config/asset lookup — when the DLL is loaded from a subfolder such as `_storage_` with no `DLSS5-AMD` next to it, also look next to the game EXE (avoids reading stale development configs).
- **Bit-exact**: kernels and weights unchanged.
- Does not address the RE9 same-command-list pre-upscale limitation.

## 0.24.2 (09-19)

Download: [OptiScaler](https://pan.quark.cn/s/f74aaa5c7f9a) (HIP)

- **Changed**: the add-on no longer reads config and locks the task table on every draw when there is no pre-upscale task; less log-counter contention; F6-off bypasses pre-upscale capture and colour copy. Repacked the same day to add the FPS/status display switch.
- **Effect**: Stellar Blade hub about 49 fps (was about 31, CPU overhead).
- **Bit-exact**: kernels and weights unchanged.

## 0.25 (09-19)

Download: [Magpie](https://pan.quark.cn/s/09630ed99606) · [OptiScaler](https://pan.quark.cn/s/636691131c5f) (HIP)

- **Changed**: C32/multi-head attention reuse the exponent; simpler reciprocal; intermediate features and decoder output passed as FP8 bytes; full decoder groups take a fast path. Fixed a missed tail write on the 900 tier and loading from CJK paths. **New gfx1200 (9060/XT) kernels**, selected automatically alongside gfx1201.
- **Effect**: Magpie 1080p DLSS5-only about 37 fps, roughly unchanged.

## 0.26 (09-19)

Download: [Magpie](https://pan.quark.cn/s/7ce2ca11db43) · [OptiScaler](https://pan.quark.cn/s/c880a70f0824) (HIP)

- **Changed**: FFN reads FP8 byte fragments directly (no shared-buffer staging, two fewer syncs); C256 weights pre-arranged into contiguous matrix fragments at init. Added the missing R11G11B10 decode shader (fixes Lies of P black screen at low effects quality); shader compile/bind checks.
- **Checks**: package files and all 44 shader variants verified.

## 0.26.1 (09-20)

Download: [OptiScaler-REFramework](https://pan.quark.cn/s/624c87a6aa11) (HIP, RE9 only)

- **Changed**: post-upscale HIP route for RE9-style integrations — R10G10B10A2/FP16 conversion, FSR post-processing, fixed 900P compute, 1080P SDR output guard; status/resolution/Present FPS and F7 info toggle. Bundles REFramework, OptiScaler, ReShade, full model and both architectures' kernels, with the 0.26 optimisations.
- Only tested on Resident Evil 9; use the general packages for other games.

## 0.27 (09-20)

Download: [Magpie](https://pan.quark.cn/s/ec3a3282aa76) · [OptiScaler](https://pan.quark.cn/s/004278159ed8) · [OptiScaler-REFramework](https://pan.quark.cn/s/010683548f68) (HIP)

- **Changed**: exact streaming ViT attention (less intermediate storage and re-reading, same math and rounding). Optional R3 adaptive reuse (change detection, longer cache when static, fused submit), **off by default**.
- **Bit-exact**: streaming ViT matches; adaptive reuse is an optional lossy switch.
- 44 shader variants per package and per-file ZIP checks passed; no INT4/pruning.

## 0.28 (09-22)

Download: [Magpie](https://pan.quark.cn/s/11547f398eb4) · [OptiScaler](https://pan.quark.cn/s/f7f423b0ea3a) (HIP)

- **Changed**: six lossless kernel optimisations — shared RGB reads, C128/C256 zero-padding skip, fixed-size ViT expand/projection and decoder projection.
- **Effect**: Stellar Blade image/fps roughly unchanged.
- The RE9 0.28 download was withdrawn in favour of 0.28.1.

## 0.28.1 (09-22)

Download: [OptiScaler-REFramework](https://pan.quark.cn/s/1375693a0d21) (HIP, RE9 only)

- **Changed**: oversized real inputs rejected before HIP init while keeping the original upscaler; failed init rolls back safely and recovers when the size is valid again; in-flight frames protected. Host and runtime must be updated together; source and TheAutomatic's credit ship with the package.
- **Checks**: 10-group / 12-submitted-frame regression and first user play passed.

## 0.29 (09-23)

Download: [Magpie](https://pan.quark.cn/s/fe1b6af36cad) · [OptiScaler](https://pan.quark.cn/s/209e04e7acaf) · [OptiScaler-REFramework](https://pan.quark.cn/s/505d38a63a85) (HIP) · [Google Drive mirror](https://drive.google.com/drive/folders/1VPsX33sLTxBG4J8kJ_IzBDlkbBTCc5Eo?usp=sharing)

- **Changed**:
  - Inputs above 1920×1080 are no longer rejected: scaled into the 1080 tier, then restored to the original resolution the way the original codec does (`DLSS5_FIT_LARGE=1`, issue #6).
  - Six bit-exact kernel optimisations since 0.28: fence scope, folded C32 FFN, byte chain + vectorised input, register-resident attention, in16 aliasing, transposed FFN tail.
- **Effect**: kernels about −7%; Stellar Blade 900P about 60 fps, 2K Native AA 44 fps; RE9 Native AA works (ultrawide not tested).
- **New switch**: `DLSS5_FIT_LARGE=1` (on in templates).
- RE9: host unchanged, runtime updated.

## 0.30 (09-25)

Download: [Quark](https://pan.quark.cn/s/80a735ab9f88) · [Google Drive mirror](https://drive.google.com/drive/folders/1pKZpLosgJXxUOZTMg_m0sbCipX9Q3WYo?usp=sharing) (three packages)

- **Changed**:
  - Any-order chain launches with per-tile flags (home-made programmatic dependent launch, `DLSS5_HIP_PDL=1`) on the C64–C256 chain.
  - Full-row FFN stores.
  - Cyberpunk 2077 zero-config: regular `OptiScaler.ini` has `[Inputs] EnableFfxInputs=false`; `DLSS5_PRE_UPSCALE_ASYNC=auto` (2077 synchronous: transient aliased colour buffer); `DLSS5_STRENGTH=auto` (2077 transfers luma only).
  - Fixed neural processing disappearing after tier/resolution changes (pinned FSR context).
- **Effect**: PDL 900P about −1.6%, 1080P −0.6%; full-row FFN −0.6%. Stellar Blade 900P→2K 60–61, 1080P→2K 47–48; 2077 low Balanced 51–52, Quality 41.
- **New switches**: `DLSS5_HIP_PDL=1`, `DLSS5_PRE_UPSCALE_ASYNC=auto`, `DLSS5_STRENGTH=auto` (template defaults).
- **Bit-exact**: PDL and full-row FFN are bit-exact.
- **Folder**: `results/pdl-chain-20260925`.
- RE9 package: same kernels, host/runtime as 0.29.

## 0.31 (09-26)

Download: [Quark](https://pan.quark.cn/s/e76b8611e3cc) · [Google Drive mirror](https://drive.google.com/drive/folders/1xtBe_XhgF9eqBlrlIQMgWcEkzm0UKHIZ?usp=sharing) (three packages)

- **Changed**:
  - Tier selection: an input within 110% of a tier on both axes is scaled down into it (2K Quality 1707×961 → 900 tier; previously upscaled into 1080).
  - Wave-owned kernels (whole C32/C64/C128 blocks + C256 attention, `DLSS5_HIP_WAVE_OWNED=1`).
  - C512 QKV/mix at 32 tokens (`DLSS5_HIP_C512_M32=1`).
  - ViT projection 64 columns (`DLSS5_HIP_VIT_PROJ_N64=1`).
  - Regular package defaults `DLSS5_VIT_ADAPTIVE=1` (adaptive reuse: +3 fps when static, off automatically in motion).
  - Five new modules (c32-wave1, c64-wave2, c512-m32-mh, c512-m32-deep, vit-wide-deep); 29 modules per architecture.
- **Effect**: wave-owned about −6% network; C512 M32 about −1.3%; ViT N64 about −1.8% at 1080. Stellar Blade main menu 2K Quality 57, 2K Native AA 43–44.
- **Bit-exact**: the three kernel changes are bit-exact; tier selection changes which tier 2K Quality uses (behaviour change); VIT_ADAPTIVE is lossy reuse (can be turned off).
- **Folders**: `results/c64-wave2-20260926`, `wave-owned-*`, `c512-ffn-20260926`, `m32-sweep-20260926`.
- RE9 package: host/runtime as 0.30; new kernels shipped but not enabled.

## 0.32 (09-26)

Download: [Quark](https://pan.quark.cn/s/b805e071405c) · [Gofile mirror](https://gofile.io/d/CZ67LYIc) (three packages)

- **Changed**:
  - VRAM pool: the driver never returns D3D12 shared buffers imported by HIP, so they are now reused per tier (40 switches: +3 GB → flat).
  - C32 wide reads (vector input loads).
  - RE9 runtime reads flags (`DLSS5_HIP_*` / `SKIP_BLOCKS` / `FIT_LARGE` / `NETWORK_HEIGHT`), enables the 0.31 kernels by default, stays compatible with the old two-argument `EnqueueHip`; PR #9 merged.
- **Effect**: C32 wide reads −0.8% / −0.9%; offline 900 tier 10.74 ms, 1080 tier 15.01 ms; RE9 medium 2K Quality 54, native AA 38.
- **Bit-exact**: C32 wide reads are bit-exact.
- **Folders**: `results/vram-leak-20260926`, `c32-wave-phase-20260926`, `re9-runtime-flags-20260926`.

## 0.33 (09-27)

Download: [Quark](https://pan.quark.cn/s/6bb64e46ab67) · [Gofile mirror](https://gofile.io/d/8yAjJX1b) (three packages)

- **Changed**:
  - FP8 packing: `CW_PACK8` in c32-wave1, `W2_PACK8` in c64-wave2 — one `cvt_pk` converts two values into a fragment word.
  - Add-on: AE/EXACT on the pre-upscale status line; F7 toggles on-screen text; shared environment-option parser.
- **Effect**: offline 900 tier 10.74→9.80 ms, 1080 tier 15.01→13.64 ms (about −9%). Stellar Blade 1080p native AA EXACT main menu 49–50, common scenes 53–54.
- **Bit-exact**: yes, against 0.32.
- **Folders**: `results/c64-block-fused-20260927`, `pack8-20260927`.
- RE9: host/runtime as 0.32 (modules only).

## 0.34 (09-27)

Download: [Quark](https://pan.quark.cn/s/4b572b0a5b81) · [Gofile mirror](https://gofile.io/d/cfHqVzD1) (three packages)

- **Changed**:
  - fmed3 clamps (`HIP_FMED3_CLAMP`, C32 `HIP_FP8_SAT_MODE 3`) and `W2_PACK8 6` (segmented FP16_OVFL + fma(x,y,+0) packing); the full module set is built from the production recipe by `hip/build-modules.ps1` (3 fallback modules rebuilt from source).
  - PDL counter rollover guard; PDL buffers freed on teardown.
  - RE9 host aa3761f2: follows the queue that actually executes the split list + 8-evaluation watchdog (Onimusha); runtime ca6d6bdc: resize leak 35 MB→0, geometry logged on every size/tier change; host/runtime source archive regenerated.
- **Effect**: Stellar Blade 1080p AA EXACT main menu 50–51 / scenes 54; RE9 medium 2K Quality 58, native AA 41; Onimusha 2K Quality about 60.
- **Bit-exact**: yes, against 0.33.
- **Folders**: `results/fmed3-ovfl-20260927`, `ovfl-census-20260927`, `c64-hand-asm-20260927`, `pdl-audit-20260927`, `onimusha-presr-20260927`, `re9-runtime-leak-20260927`.

## 0.35 (09-27)

Download: [Quark](https://pan.quark.cn/s/83e6172e6c79) · [Gofile mirror](https://gofile.io/d/NnF4GitT) (three packages)

- **Changed**:
  - Three C32 rounds: duplicate FP8 round trips removed (`CW_DIRECT_OUT`, `CW_PREFIX_DIRECT_OUT`), vectorised RTZ/LDS stores (`CW_RTZ_PAIR`), per-segment saturation mode (`CW_PACK_MODE_MASK`), full-window fast paths in prefix/finish (`CW_PREFIX_FULL_TILE` etc.).
  - ViT byte stream (`DLSS5_HIP_VIT_STREAM=3`, new `vit-stream` module: attention output goes to the projection as bytes; works with adaptive reuse).
  - Add-on 4151123e; 30 modules per architecture; RE9 runtime 432d8ccf (same switch; host aa3761f2 unchanged).
- **Effect**: offline 900 tier about 9.3 ms, 1080 tier about 12.75 ms. Stellar Blade 1080p AA EXACT about 56.7 (0.34: 54); RE9 medium 2K Quality 58–59, native AA 42.
- **New switch**: `DLSS5_HIP_VIT_STREAM=3` (template default).
- **Bit-exact**: all changes bit-exact against 0.34.
- **Folders**: `results/c32-aco-20260927`, `c32-round2-20260927`, `c32-round3-20260927`, `vit-bytestream-20260927`.

## 0.36 (09-28)

Download: [Quark](https://pan.quark.cn/s/e5afdaca0769) · [Gofile mirror](https://gofile.io/d/Z1hWdjcB) (all three packages)

- **Summary**: add-on d2290ad7; 30 modules per architecture; RE9 runtime 7ce2bc21 (host aa3761f2 unchanged); input shader `native_game_rgb_input.hlsl` 5be59a41 (matches the direct input write).
- **Cumulative effect (vs the 0.35 release packages, two ABBA rounds in one batch)**: offline 900 tier 9.37 → 8.58 ms (−0.79 ms, −8.5%), 1080 tier 12.71 → 11.58 ms (−1.13 to −1.14 ms, −8.9%). Kernel launches per frame at 1080: 214 → 182.
- **Bit-exactness**: **0.36 is not bit-identical to 0.35** — the float FMA activation (item 4 below) is a deliberate numeric change with essentially unchanged error against NVIDIA's reference; 0.36 is the new bit-exact baseline. Every other item was bit-exact against the build before it.
- **New switches**: `DLSS5_DIRECT_IO` (1 in the regular/Magpie templates, not set for RE9), `DLSS5_FRAME_STATS=<seconds>` (0 in templates).
- **What changed** (in order):

1. **C64–C256 deep pass** (c64-wave2 recipe: whole-group byte input reads, RTZ pairing, direct coordinates): offline 900 −0.7%, 1080 −0.8%; bit-exact; modules only. `results/mh-round1-20260927`, `tier900-20260927`. Stellar Blade 1080p AA EXACT 57.1.
2. **Frame-time distribution log** `DLSS5_FRAME_STATS=<seconds>` (add-on + RE9 runtime, writes `DLSS5-AMD\logs\frame-stats.txt`, 0 in templates). `results/frame-stats-20260928`.
3. **Two cuts from a line-by-line ACO comparison**: removed a redundant NaN canonicalisation before the C32 activation; bounded reciprocal in C64–C256 softmax (sum order and error correction kept). 900 −0.82% / −0.65%, 1080 −0.75% / −0.74%; bit-exact. `results/aco-lineup-20260928`.
4. **float FMA activations (a deliberate numeric change; the bit-exact baseline changes on 09-28)**: the multiply-add in the C32, C64–C256 and ViT/C512 activations is contracted to a float FMA (previously two roundings, kept to match HLSL `precise`; the original NVIDIA code uses half FMA). 900 saves 0.110–0.115 ms, 1080 0.148–0.151 ms (about 1.2%); error against the original NVIDIA output essentially unchanged (single-frame RMSE 0.008038→0.008037). This output is the bit-exact baseline from now on. `results/fma-vs-nvidia-20260928`, `float-fma-20260928`.
5. **Direct input / direct output** `DLSS5_DIRECT_IO` (0 old path, 1 input written straight into the HIP shared buffer, 3 also lets FSR read the decode output directly): one 35 MB copy and one copy-back removed; offline −0.02 to −0.05 ms; network output bit-exact. Temporal sessions (Magpie), `DLSS5_OVERLAP` and non-RGBA16F formats fall back automatically; RE9 not affected. `results/zero-copy-io-20260928`.
6. **C256 whole-block fusion** (several token groups share one weight read; FFN weight reads halved; main kernel VGPR 190→154): 1080 −1.67% / −1.63% (about −0.20 ms), 900 keeps the old path; dispatches per frame at 1080 214→198; bit-exact. `results/c256-fusion-20260928`.
7. **C512 fusion + C64/C128 weight sharing**: one dispatch fewer per C512 block (1080 198→185, 900 214→201); C64/C128 weight reads halved. 900 −3.59% (about −0.33 ms), 1080 −2.55% / −2.63% (about −0.31 ms); bit-exact. Offline 1080 tier about 11.82 ms, 900 tier about 8.72 ms. `results/c512-fusion-20260928`.
8. **Upsample fused into the first block**: the C64/C128 and C32 upsamples merged into the next level's first block, 3 fewer launches per frame (1080 185→182, 900 201→198); 900 −0.17 to −0.18 ms, 1080 −0.25 to −0.26 ms; bit-exact. ViT and downsample candidates measured no gain and were not merged. `results/fusion-round3-20260928` (with `package-036-checklist.md`).

In game (local, RX 9070 XT, EXACT, standing still): Stellar Blade 1080p native AA about 57 → 60 (at the 60 Hz windowed-mode cap); 2560×1440 native AA 52–53 after the C512 fusion, 54 final (the in-game comparison setting from now on).


## 0.37 (09-29)

Download (all three packages): [Quark](https://pan.quark.cn/s/7dbfdc6425fd) · [Gofile mirror](https://gofile.io/d/onqeAHST)

- **Summary**: add-on b77bbc3c; 31 modules per architecture (new `swin-persistent.hsaco`); RE9 runtime 2c103f6e (host aa3761f2 unchanged); shaders identical to 0.36 (input shader 5be59a41).
- **Bit-exact**: **fully bit-identical to 0.36** (09-28 float-FMA baseline; 7 cases, 168 EXACT/AE frames plus rollover/timeout stress frames, checked at every step). Nothing lossy.
- **Cumulative**: network offline 900 tier about 8.5 → 8.0 ms; launches per frame 900 tier 198 → 179, 1080 tier 182 → 162. Stellar Blade 2560×1440 native AA, EXACT 54 → 55–56 (local).
- **New switch**: `DLSS5_HIP_SWIN_RUN` (C256 persistent stage; source default 0, **set to 1 in all three release templates**; the RE9 runtime reads it too; 0 restores the previous launches).
- **Package extras**: the regular OptiScaler package now also ships `ReShade.ini` (`TutorialProgress=4`, no Home-key tutorial overlay), like Magpie; the RE9 source archive now includes the HIP recipes' `.inc` files.
- **Changes** (in order, each bit-identical to the step before):

1. **C512 compact point-wise layout**: FFN/conv run on the effective tiles only (1080 tier 160 → 135 4×4 tiles); shifting and padding moved to the attention read; windows and softmax unchanged. 900 −0.16 to −0.18 ms, 1080 −0.23 to −0.25 ms (about −2% each); 1080 launches 182 → 169. `results/deep-layers-20260929`.
2. **Kernel-map cuts**: after a full per-kernel 1080 comparison (our 169 vs reference 154 launches), C512 head pooling + projection fused into one launch (head group fusion) and the ViT attention score layout transposed. 900 −0.02 to −0.04 ms, 1080 −0.12 to −0.14 ms (1.0–1.2%); launches 198 → 197 / 169 → 168. `results/kernel-map-20260929`.
3. **C256 persistent stage** (`DLSS5_HIP_SWIN_RUN=1`): the six inner layers of each C256 encoder/decoder stage run from a device ready queue; a ~100 ms timeout replays the stage serially on the GPU and disables persistence for the instance, so no bad intermediate leaves the network. 900 −1.90 to −1.94% (about −0.16 ms), 1080 −0.57 to −0.61%; launches 197 → 179 / 168 → 162. `results/swin-persistent-20260929`.
4. **New ViT attention kernel**: exact hardware widening for bounded normal halves, probabilities encoded in register pairs, denominator/AV output transposed. 640-token kernel about 71 → 38 µs; network 900 −2.20 to −2.28% (about −0.19 ms), 1080 −1.50 to −1.58% (about −0.17 ms). `results/vit-attention-20260929`.
5. **ViT QKV, five waves sharing weights** (W5): five waves share one weight copy through LDS (8 KB double-buffered blocks), reads divided by about 5 without reducing waves; 640-token kernel 48.8 → 37.0 µs. Network 900 −0.42 to −1.39%, 1080 −0.74 to −1.07%. The new host detects the export and falls back with older modules. `results/vit-qkv-20260929`.

Not shipped (negative results): C128/C64 persistence (below the 0.5% bar, `results/swin-persistent-c128-c64-20260929`), C512 projection weight sharing (bit-exact but slower, `results/c512-proj-share-20260929`), the `MAKE_RESIDENT` spike (not reproduced offline, `results/resident-spike-20260929`). Package checklist `Development/results/package-037/checklist.md`.

## 0.38 (09-30)

Download (all three packages): [Quark](https://pan.quark.cn/s/6856d875bbe9) · [Gofile mirror](https://gofile.io/d/lzsqfUiE)

- **Bit-exact**: **with default settings fully bit-identical to 0.37** (09-28 float-FMA baseline; 7 cases, 168 EXACT/AE frames plus ticket rollover, checked at every step; the add-on and RE9 runtime shipped were rebuilt from the release source and re-checked against the installed build on the same 168 frames + rollover, all identical). The only lossy item is the opt-in `DLSS5_NETWORK_1080_ROWS=1088`, off by default.
- **Effect** (offline full-network replay, NativeGameFrame, 1000 frames minus the first 200, ms per frame): 900 tier about **8.0 → 7.6 ms**, 1080 tier about **10.8 → 10.4 ms** (release modules measured 900 7.551/7.606, 1080 10.413/10.458, two runs, `results/hip-roofline-20260930` section 1; per-kernel map `results/kernel-map-v3-20260930`). Launches per frame: 900 tier 179 → 162, 1080 tier 162 → 158. In-game runs (Stellar Blade, Onimusha) are correctness checks only (no flicker, artefacts or crashes); frame-rate readings are not used as evidence.
- **New switches**:
  - `DLSS5_FORMAT_FALLBACK` (1 in all three templates, source default 1): colour formats that used to be rejected — R9G9B9E5, B8G8R8X8, R10G10B10A2, R32G32B32(A32), R16G16B16A16/R8G8B8A8 SNORM, B5G6R5, B5G5R5A1, B4G4R4A4 — are converted to RGBA16F by one compute pass on the regular package's pre-upscale route (new `native_format_convert.hlsl`) and then take the normal route; the RE9 runtime uses its private FP16 output route. Formats accepted before never reach the table (bit-exact). Rejected formats are logged by name. Post-upscale / Magpie / XeSS routes unchanged. 0 = original table. `results/product-fmt-reload-20260930`.
  - `DLSS5_HOT_RELOAD` (1 in the regular/Magpie templates, source default 1; not applicable to RE9): editing `DLSS5_STRENGTH` / `DLSS5_NOTICE` / `DLSS5_SHOW_FPS` in the flags file while the game runs applies within a second; other keys still need a restart. No edit, no change.
  - `DLSS5_NETWORK_1080_ROWS` (templates 1152 = NVIDIA geometry): **opt-in `1088`, LOSSY, off by default** — the 1080 tier computes 1088 rows, network about −0.47 ms (4.4%), about 56 dB vs 1152 over the frame, bottom 32 rows slightly worse. `results/geom-1088-20260930`.
  - Internal host switches (default 1, normally untouched): `HIP_C512_PAD16`, `HIP_C256_FFN_W16`; they fall back to the old path with older modules or too-small buffers.
- **Changes** (in order, each bit-identical to the step before; figures are each step's own ABBA):

1. **900 tier: C512 shift_pack removed**: producers allocate 16-token-padded buffers (1500 → 1504), C512 blocks read in place, 13 `mh_shift_pack` launches gone; host only. 900 −0.09 to −0.11 ms, 1080 unchanged. `results/shift-pack-900-20260930`.
2. **C32 block-4 skip/downsample stored as E4M3 bytes**: their values are exact E4M3 already; traffic drops to a quarter. 900 −0.05 ms (0.62%), 1080 −0.09 ms (0.82%). `results/c32-align-20260930`.
3. **C32 diagonal residual skips all-zero halves** (`CW_DIAG_ONLY`): 1080 about −0.04 ms. `results/small-cuts-20260930`.
4. **I+P+O**: C32 input/prefix without the half round trip; occupancy caps for C512 mix and ViT contract. 900 −0.03 ms, 1080 −0.02 to −0.03 ms. `results/small-wins-retest-20260930`.
5. **Composite quantisation**: `FP8(Hrtz(x))` at WMMA byte exits becomes an integer bit mask, dropping the f32→f16→f32 round trip; proved over all 2³² inputs. c64-wave2/swin-persistent (`results/composite-quant-20260930`) and two C512 exits (`results/composite-quant-c512-20260930`), −0.005 to −0.03 ms each.
6. **C256 FFN weights in 16-byte fragments**: one 16-byte load feeds two WMMAs, k order unchanged; falls back with older modules/hosts. 900 −0.01 to −0.03 ms. `results/c256-w16-20260930`.
7. **C32 prefix down / block-69 main stored as E4M3 bytes**: 900 −0.01 to −0.02 ms, 1080 about −0.02 ms. `results/prefix-post-20260930`.
8. **Three wide-store rewrites**: C32 prefix byte tail (900 −0.04, 1080 −0.07 ms, `results/prefix-post-arith-20260930`), C32 finish byte tails (900 −0.04, 1080 −0.05 ms, `results/tail-vec-20260930`), C512 t8 byte copies via LDS transpose (about −0.01 to −0.03 ms, `results/deep-tail-20260930`).
9. **C512 QKV-attention: redundant F dropped + bounded reciprocal**: the extra clamps at the AV/QKV exits removed, softmax division replaced by rcp + two Newton steps; both tiers, three rounds −0.02 to −0.03 ms. `results/c512-av-f-20260930`.
10. **F/division clean-up across the network**: only deep_fast-packed taken (900 −0.01 to −0.04, 1080 −0.01 to −0.03 ms). `results/f-sweep-20260930`.

- **Package**: new `native_format_convert.hlsl`; the three flags templates carry the three switches above; add-on and RE9 runtime rebuilt from the release source (tag 0.38); RE9 host unchanged; RE9 source archive regenerated.
- Not shipped (bit-exact but not faster everywhere, macros default 0): D3D→HIP GPU polling `DLSS5_HIP_INPUT_POLL` (`results/handoff-gpu-20260930`), C512 V transpose (`results/c512-compact-vt-20260930`), W16 for C64/C128 (`results/w16-c64-c128-20260930`), C512 FFN LDS weight sharing (`results/c512-ffn-lds-20260930`), Infinity Cache hot reuse (`results/infinity-cache-20260930`). Package checklist `Development/results/package-038/checklist.md`.

## 0.39 (10-01)

Download (three packages): [Quark](https://pan.quark.cn/s/dea9c0ef2f95) · [Gofile mirror](https://gofile.io/d/iqtFTSpS)

- **Bit-exact**: **with default settings every output is bit-identical to 0.38** (7 cases EXACT/AE 168 frames + AE decisions + 900/1080 ticket rollover, 19 groups SAME; checked for every cut and again for the installed build as a whole).
- **Effect** (offline whole-network replay, ms per frame): 900 tier about **7.27 -> 6.8 ms**, 1080 tier about **10.05 -> 9.5 ms**. In game, Stellar Blade at 2K about **+1.5-2 fps** (55.6-56.1 -> 57-58 fps), no flicker; Onimusha fine.
- **What changed**: a batch of bit-exact kernel reorderings — C512 FFN done in registers by one wave (`C512_FFN_ONE`), attn-project residual init hoisted, C32/C64 weight loads hoisted, wider decode (`HIP_DEC_WIDE`), ViT QKV/attention fusion and more; each kept only if three ABBA rounds were faster on both tiers. Details in `Development/DevHistory.md` (09-30 evening to 10-01).
- **New switch `DLSS5_STYLE`** (`1` in all three templates, source default 1): the NVIDIA NGX Style control, `0`/`1`/`2`. Until now Style 1 was built into the preprocess step (feature 6 = Style/128), the value the original runtime used in Stellar Blade. It is now a device constant in the C32/prefix modules that the host sets after loading. `0` is NVIDIA's default and the setting their reference images use (1080p single frame vs NVIDIA: Style 1 24.06 dB, Style 0 44.26 dB with the release recipe / 47.43 dB with all 71 blocks); `2` is the third style. A value that is not exactly 0/1/2 falls back to 1. Needs a restart. Default `1`: **bit-identical**, no extra GPU work. RE9 package: the runtime does not read this key from the flags file yet (editing the template line has no effect); use a system environment variable to change the style. `results/rebuild-baseline-20261001`.
- **Reproducible builds**: the add-on and the RE9 runtime are now linked at a fixed image base without a timestamp, so the same source gives the same file wherever it is built (before, the linker derived the base address from the output path, so every rebuild differed in all absolute addresses). The add-on and RE9 runtime in this release were rebuilt byte for byte from the release source; the 62 modules were checked against the source code section by section.
- Host: an unused fast-tier probe compiled out of the default host (it cost 0.005-0.02 ms per frame); failed kernel lookups are cached.
- **In the packages**: `DLSS5_STYLE=1` added to the three flags templates; add-on, RE9 runtime and 62 modules byte-identical to the Stellar Blade / Onimusha installs; RE9 host unchanged; RE9 source archive regenerated. Packaging checklist `Development/results/package-039-20261001/checklist.md`.

## 0.40 (10-03)

Download (three packages): [Quark](https://pan.quark.cn/s/d38e0f653c5a) · [Gofile mirror](https://gofile.io/d/moSf7cqf).

- **Default output changed** (not bit-identical to 0.39): the templates now run all 71 blocks with fast numerics (`DLSS5_SKIP_BLOCKS=` empty, `DLSS5_FAST_NUMERIC=1`). Against NVIDIA (1080p single frame, Style 0) 44.26 → 47.55 dB and the overall colour shift is gone; offline about 0.12 ms (900) / 0.19 ms (1080) slower per frame than the old default. Writing `DLSS5_SKIP_BLOCKS=42,43,46` and `DLSS5_FAST_NUMERIC=0` gives back the 0.39 output bit for bit (every bit-exact change below was checked as 19 groups SAME against the previous build).
- **Multi pass** (`DLSS5_MULTI_PASS=1/2/3`, default 1): the network runs 2 or 3 times per frame on its own output, for a stronger look; cost is about N times the network. **F9** in the regular and Magpie add-on cycles 1→2→3 in game and remembers the choice in `custom-config.txt`. Stellar Blade 2K, tested in game: 57 / 37 / 27 fps for 1 / 2 / 3 passes. The RE9 runtime reads the key at start (no hotkey).
- **Three config files**: `default-config.txt` (shipped, overwritten on upgrade) → `custom-config.txt` (yours, never touched by a package) → `native-game-flags.txt` (the old file, still honoured); a `DLSS5_*` system environment variable beats all three. Two behaviour changes: in the add-on an environment variable now wins over the files (before, the file won); in the RE9 runtime a key written twice in one file now takes the last line (before, the first).
- `DLSS5_MULTI_PASS_SKIP_BLOCKS` (opt-in, lossy, default empty): skip blocks in passes 2..N only. Measured: it saves little (`42,43,46` about 0.5 ms at 3 passes) and the larger lists change the style rather than giving a cheaper 3-pass picture. Not recommended.
- **Faster, bit-exact**: the C32 and C64 window kernels are built with LLVM 23 (plus a scheduling option for C32), a C512 kernel tuned; offline 900 about −0.11…−0.14 ms, 1080 −0.18…−0.19 ms per frame. The 1080 tier gets its own C32 build with a hardware round-toward-zero conversion (1080 −0.03…−0.05 ms, other tiers unchanged). One non-blocking stream query after the output signal (−0.01…−0.12 ms); RE9 runtime writes its input directly (−0.03…−0.08 ms on a pipelined host).
- RE9 runtime: `DLSS5_STYLE`, `DLSS5_FRAME_STATS`, `DLSS5_MULTI_PASS`, `DLSS5_MULTI_PASS_SKIP_BLOCKS`, `DLSS5_FAST_NUMERIC` and `DLSS5_NETWORK_FREE_RES` are read from the config files. `GetTimings` for integrators, with the collapsed-sample fix (PDL on or off) found by TheAutomatic.
- New opt-ins (default off): `DLSS5_NETWORK_FREE_RES=1` (network at the input's own size, closer to NVIDIA above 1080p, 1.7× / 3.6× slower at 1440p / 4K).
- **In the packages**: `default-config.txt` + `custom-config.template.txt`; `custom-config.txt` and `native-game-flags.txt` are no longer shipped, so unpacking over an old install keeps your settings. 68 HIP modules (62 + the 1080 C32 build + fast-numeric builds, both architectures). Package notes updated (defaults, config files, multi pass, F9, lossy options).

Details:

- New opt-in `DLSS5_MULTI_PASS_SKIP_BLOCKS` (default empty, all three templates empty; add-on and RE9 runtime, whitelisted): blocks skipped only in passes 2..N of `DLSS5_MULTI_PASS`, same list syntax as `DLSS5_SKIP_BLOCKS`; pass 1 always runs the full configured network. **Lossy** when set. 3 passes, current default, offline: empty 21.1 / 29.8 ms (900 / 1080); `42,43,46` −0.4 / −0.5 ms, 43.9–46.0 dB; ViT + C512 up (`31`–`38`,`40`–`47`) 18.4 / 25.7 ms, 34.1–35.0 dB; + C512 down (`23`–`38`,`40`–`47`) 17.4 / 24.3 ms, 33.2–33.6 dB — the dB are against our own 3-pass all-block output, not NVIDIA's (3 passes vs 1 pass is 35.0 dB, so the larger lists change the look about as much as the extra passes do). The cost of a pass is in the full-resolution blocks, which the production pipeline cannot skip, so the saving is limited. Invalid lists (unparsable, or a C64/C128/C256 block the byte-stream pipeline cannot skip) fall back to empty with a stderr line. Empty = bit-identical (19 groups SAME; RE9 replay 900 6f961945… / 1080 aaa31e2d… unchanged).
- Add-on: `DLSS5_MULTI_PASS` can change while the game runs. The hot reload (`DLSS5_HOT_RELOAD`) now re-reads it and the HIP network switches before its next frame, and a hotkey (`DLSS5_MULTI_PASS_HOTKEY`, default **F9**, `F1`–`F24` or a key code, `0` = off; regular and Magpie templates) cycles 1→2→3→1: it writes `DLSS5_MULTI_PASS=N` into `custom-config.txt` (replaces the line or appends it, creates the file if missing, keeps BOM and line ends). If `native-game-flags.txt` also sets the key, that line is rewritten too (stderr line), since it would win over custom; a system environment variable wins over every file (stderr line, no effect). Logged in `logs\native-game-oneshot.txt` (`event=multi_pass_hotkey`, `multi_pass=N` in the hot-reload line). Not pressed or off = bit-identical. RE9 runtime: not applicable (no hot reload there).
- Three config files instead of one. The `DLSS5-AMD` folder is read as `default-config.txt` (the package template, overwritten on upgrade) → `custom-config.txt` (the user's changes, never written by install or upgrade) → `native-game-flags.txt` (the old file, same meaning as before, so existing installs keep working); a later file overrides a key of an earlier one, unmentioned keys keep the earlier value, missing files are skipped, and a `DLSS5_*` system environment variable wins over all files. Same key twice in one file: last line wins. Empty value (`KEY=`) overrides the earlier files and means the built-in default. One shared reader (`src/native_config_layers.h`) now serves the regular and Magpie add-on (environment loader and every key read straight from the file: notice, snapshot frame, pre-upscale, fit, upscaler choice, VRAM reservation, format fallback), the hot reload (it now watches all three files) and the RE9 runtime. Behaviour changes: in the add-on a system environment variable now wins over the files (before, the file overwrote it); in the RE9 runtime a key written twice in one file now takes the last line (before, the first); lines longer than 255 bytes are no longer cut. The RE9 runtime now also reads `DLSS5_FRAME_STATS` from the files (before: environment only). Packages ship the template as `default-config.txt` plus `custom-config.template.txt` and no longer ship `native-game-flags.txt` or `custom-config.txt`, so unpacking over an old install leaves the user's files alone. With unchanged files the output is unchanged (19 groups SAME, RE9 900/1080 SAME). *Integrator note:* a host that ships its own `native-game-flags.txt` keeps working; a folder now counts as the add-on's config folder when any of the three files is present.
- New option `DLSS5_MULTI_PASS` (multi pass, "叠层"; default `1`, all three templates `1`; regular add-on, Magpie add-on and RE9 runtime): `2`/`3` run the whole network 2/3 times per frame, each pass fed the previous pass's final RGB (alpha 1) with the same history, seed and noise, as in Magpie 0.6.8's DLSSNR Multi Pass; the style gets stronger with each pass. Implemented once in the shared network layer (`hip_reference_network.h`, `Network::EnqueueRaw`), so all three routes get it from the same line. The network output is already the clamped [0,1] picture in the input's working encoding, so the decode→encode round trip between passes is the identity and is skipped. Offline whole network: 900 tier 7.0 → 13.7–13.8 (2) / 20.4–20.6 ms (3), 1080 tier 9.8 → 19.5 (2) / 29.0 ms (3); against the single pass 38.4–39.1 dB (2) / 34.2–34.7 dB (3); two runs hash-identical, no non-finite values. Default `1`: same code path, output bit-identical (19 groups SAME, RE9 900/1080 SAME, smoke 0), ABBA neutral. *Integrator note:* `GetTimings`/`net_gpu_ms` report the total of all passes, not one pass; extra VRAM one (`2`) or two (`3`) RGBA f32 buffers of processing width × height × 16 bytes (35.4 MB each at the 1080 tier; measured process VRAM in the RE9 runtime +28/+75 MiB at 900, +111/+111 MiB at 1080); adaptive ViT reuse is forced off while N>1; read when the network is created (session/network rebuild to change it); no per-pass Style/strength. Invalid values fall back to 1 with a stderr line. The RE9 runtime reads it from the flags file (whitelist). `results/multi-pass-20261003`.
- Default configuration changed to all 71 blocks + `DLSS5_FAST_NUMERIC=1` (all three templates: `DLSS5_SKIP_BLOCKS=` empty, `DLSS5_FAST_NUMERIC=1`). Against the previous default (skip 42,43,46, bit-exact numerics): offline ABBA about 0.12 ms (900) / 0.19 ms (1080) slower per frame; against NVIDIA (1080p single frame, Style 0) 44.26 → 47.55 dB and the overall colour shift is gone; on motion the output is about 3 dB closer to the bit-exact all-block output than with the block skip (worst frame 52.68 vs 50.69 dB). The block skip stays available as an optional speed-up: `DLSS5_SKIP_BLOCKS=42,43,46` gives back about 0.20 ms (900) / 0.32 ms (1080) for about −3.3 dB. The RE9 runtime's built-in skip default is now empty too: a flags-file line `DLSS5_SKIP_BLOCKS=` removes the variable, and the older runtime then fell back to its built-in 42,43,46, so an empty value could not turn the skip off there. *Integrator note:* the default output is no longer bit-exact (it was not identical to NVIDIA's network before either, because of the skip); set `DLSS5_FAST_NUMERIC=0` for the bit-exact path. Hosts that ship their own flags file or no flags file: the RE9 runtime without a `DLSS5_SKIP_BLOCKS` line now runs all 71 blocks (about +0.20/+0.32 ms against the old behaviour; write `42,43,46` to keep it), and `DLSS5_FAST_NUMERIC` still defaults to 0 in the source, so it needs the line in the file (or the environment) to get the new default. Empty and absent `DLSS5_SKIP_BLOCKS` now mean the same in the add-on and the RE9 runtime. `results/default-swap-20261003`, `results/default-c-20261003`.
- New opt-in `DLSS5_FAST_NUMERIC` (default `0`; the templates now set `1`, see the default change above; add-on and RE9 runtime): `1` loads lossy fast-numeric builds of the C32 and C64/C128 window kernels (`c32-wave1-fast.hsaco`, `c64-wave2-fast.hsaco`: f32 activation/normalisation instead of the half-precision rounding steps, softmax 1/sum by `rcp`) for every tier. Offline ABBA 900 −0.07…−0.13, 1080 −0.09…−0.10 ms per frame; against the bit-exact output 51.8 dB worst frame, 53.1–55.5 dB per sequence; against NVIDIA (1080p single frame, Style 0) 44.26 → 44.23 dB with the release block skip, 47.43 → 47.55 dB with all 71 blocks. Default output bit-identical (19 groups SAME, RE9 900/1080 SAME), ABBA neutral. *Integrator note:* two new module files per architecture next to the others (the package and `SHA256SUMS` grow by four files); the hosts open them only when the option is `1`, and a missing file falls back to the normal module with a stderr line. Numeric approximation: the error passes through every following block and can build up, unlike the geometry options; nothing carries over between frames. Read when the network is created. Replaces the module-swapping fast-tier switch of 10-01. `results/fast-numeric-option-20261003`.
- HIP bridge (add-on and RE9 runtime): one non-blocking `hipStreamQuery` right after the network's output signal, so Windows HIP submits the whole batch (network + signal) at once. Bit-identical (19 groups SAME); offline ABBA faster in all six rounds, 900 −0.01…−0.10, 1080 −0.05…−0.12 ms, p99 better on both tiers. New switch `DLSS5_HIP_POST_SIGNAL_QUERY`, default 1, not in templates; 0 turns it off. *Integrators*: no ABI or call-order change. `results/outside-net-20261002`.
- RE9 runtime: direct input. The RGB input pass now writes the HIP bridge's shared input buffer itself, so `RecordInputs` no longer records the 35 MB (1080 tier) `CopyBufferRegion`; same switch and default as the add-on since 09-28 (`DLSS5_DIRECT_IO` bit 1, default 1). `RecordInputs` GPU time 0.22 → 0.08 ms (1080), 0.10 → 0.06 ms (900); a pipelined host (no per-frame GPU wait) is faster in all six ABBA rounds, 900 −0.03…−0.04, 1080 −0.07…−0.08 ms per frame; a serial host that waits every frame is neutral (there the HIP start is bound by the ~0.3 ms CPU kernel launch in `EnqueueHip`). Output bit-identical at 720/900/1080 and 1600×900/1920×1080, smoke 0. *Integrators*: no ABI, call-order or list changes; `RecordInputs` just records one copy and two barriers fewer; `DLSS5_DIRECT_IO=0` (environment or flags file) restores the old path. Call-order advice: `include/LmxxfNrApi-call-order.md`. `results/outside-net-20261002`.
- New opt-in `DLSS5_NETWORK_FREE_RES` (default `0`, all templates `0`; add-on and RE9 runtime): `1` runs the network on the input's own size instead of snapping to the 720/900/1080 tiers. Padding follows NVIDIA's plan walk (each axis to a multiple of 64, the width +64 when both are multiples of 256, a 1088-row result taken to 1152 as in NVIDIA's captured 1080 call), the input stays 1:1 at the top-left and the padding is mirrored. Default output bit-identical (19 groups SAME, RE9 900/1080 SAME), ABBA neutral. Against NVIDIA's NGX single frames (Style 0, all blocks): 1440p 35.9 → 46.4 dB, 4K 33.3 → 48.5 dB; 1920×1080 and 1600×900 inputs give exactly the tier output. *Integrator note:* cost scales with the processed pixels — 1440p about 1.7×, 3440×1440 2.3×, 4K 3.6× the 1080 tier's frame time; admitted range 320×320 up to a 3840×2176 processing surface (outside it the tiers and `DLSS5_FIT_LARGE` apply as before); overrides `DLSS5_NETWORK_HEIGHT`/`DLSS5_NETWORK_1080_ROWS`; read when the network is created, so a resolution change rebuilds it; HIP backend only. `results/free-res-20261002`.
- RE9 runtime: network GPU time for integrators. `LmxxfNrApi` gains `GetTimings(context, LmxxfNrTimings*)` at the end of the function table (`struct_size`, `valid`, `network_ms`, `frame_id`), and `GetStatus` appends `net_gpu_ms=X.XX (frame N)`. Measured with hipEvents on the HIP stream after the producer wait and before the consumer signal; read without blocking, so it is the most recent completed frame (normally N-1). Default output bit-identical. *Integrator note:* `network_ms` is the neural network only — not the D3D12 input copy, codec passes or handoff waits — so it is not comparable with D3D12 timestamp queries around RecordInputs/RecordOutputs (those see only the D3D12 passes; the network does not run on that queue). Frames where the adaptive ViT reuse skips the ViT part are shorter; passthrough/fallback frames are not timed. ABI version unchanged: a host built against the old header passes `LMXXF_NR_API_V1_SIZE` (136) and keeps working; a new host talking to an older runtime gets `LMXXF_NR_INVALID_ARGUMENT` for the full size and should retry with `LMXXF_NR_API_V1_SIZE` (no GetTimings). Timing starts at the first `GetTimings` call (that call returns `valid=0`); recording events on every frame from session start measured +0.01..+0.10 ms, so sessions that never ask pay nothing and `GetStatus` shows `net_gpu_ms=off`. Rare single samples are far too short (first frame after a (re)create, ~1 in 1200 others): show a median over a few frames. Call it from the thread that drives the frames. Fixed after release review: on some Windows HIP setups the samples could collapse to ~0.001 ms for many consecutive frames (PDL on or off); the bridge now does one non-blocking `hipEventQuery` on the end event right after recording it, before the output signal (no wait, no extra GPU dependency). Found, isolated and verified by TheAutomatic — thanks. The regular add-on shares the bridge code but does not enable timing (`DLSS5_NET_TIMING=1` turns it on for diagnostics). `results/net-timing-20261002`.
- RE9 runtime: `DLSS5_STYLE` is now read from the flags file like the other network keys (0.39's runtime ignored the template line and only took it from the system environment). Default `1`: bit-identical; `0` from the file gives the same output as `0` from the environment. *Integrator note:* read once at session creation, not hot-reloaded; an environment variable of the same name wins; changes the RE9 runtime file, nothing else. `results/night-20261001`.

## 0.41 (10-05)

Download (three complete packages): [Quark](https://pan.quark.cn/s/dbda3e470f8f) · [Gofile mirror](https://gofile.io/d/YAENU0ex).

The default remains **1x** (`DLSS5_MULTI_PASS=1`). When 3x is selected, two real passes plus a locally predicted third are enabled by default (**lossy**); set `DLSS5_MULTI_PASS_PREDICT=0` for three real passes. Existing custom/native/environment overrides are preserved. Skin protection defaults to 0.

- **Broader fast numerics**: FAST_NUMERIC=1 now also selects ViT/deep fast twins. This changes arithmetic at 1x/2x too; it is not bit-identical to the 0.40 default. Three offline ABBA rounds at 900/1080 all faster, approximately 0.09/0.10 ms per frame (both sides using the historical skip-42,43,46 benchmark contract). Against the normal path: worst frame 52.1 dB, sequences 53.0–55.6 dB. The previous 44.26→47.55 dB NVIDIA comparison measured the earlier C32/C64 set, not this extended set. `results/fast-vit-c512-20261003`.
- **Fast 3x prediction**: two real network passes and an RGB-shared local third-pass fit, not a NVIDIA exact claim. Actual GPU quality against our real 3-pass output: four fixtures 49.36–49.83 dB, history 45.28 dB; history dark/colour regions can be slightly worse. First-pass/feed/predict/skin resources are prepared before the producer wait, fixing the first-frame synchronous-upload deadlock seen in Onimusha. Add-on hot reload supports prediction; RE9 needs a restart. `results/multi-pass-predict-20261004`.
- **Bit-exact data-flow improvements**: intermediate post output directly feeds RGBA to the next pass; ViT's already-quantised contract edge stays in FP8 bytes into QKV/projection; active C512 FFN avoids decoding and repacking the same FP8 code. C512's latest same-batch single-pass gain: 900/1080/1440 about 0.023/0.042/0.103 ms, all three ABBA rounds faster with improved pooled p99. These separate experiment gains are not added together or presented as a package FPS promise. `results/multipass-direct-rgba-20261004`, `results/vit-byteedge-formal-20261004`, `results/c512-direct-whole-20261004`.
- **Native 1440p**: enables the existing whole C256 fusion for the actual 2560×1472 processing surface, without lowering resolution. Its own same-batch single-pass wall time 17.483→17.029 ms and fast-3x wall time 34.409→33.442 ms exclude game rendering/FSR/Present. Free-resolution support itself was already in 0.40. `results/native-1440-optimization-20261004`.
- **Integration fixes**: `DLSS5_PRE_UPSCALE=auto` probes the command-list contract and falls back to post-upscale when required. HIP Enqueue rebinds its selected device on the calling thread, adopting [XMoon's PR #15](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting/pull/15); contributor tests with iGPU+dGPU went from nine InvalidHandle errors to 600+ frames without errors. The local single-HIP-device checks do not independently reproduce that dual-device setup.
- **RE9 strength configuration**: valid `DLSS5_STRENGTH=a,b` file/environment values (each 0..1) override host/menu tone controls; auto/empty/absent continues host/default behaviour. Add-on/Magpie still accept 0..3. No new ABI, default strength or network Style change. RE9 files require restart; add-on strength hot reload remains available. `results/strength-config-20261004`.
- **Optional skin protection**: `DLSS5_MULTI_PASS_SKIN_PROTECT=1` uses the first real pass in skin-mask cores and the final pass elsewhere. It is a colour heuristic, not segmentation; warm backgrounds/coloured lighting and history feedback limit it. User feedback found little overall benefit, so it stays off by default. `results/skin-protect-20261004`.
- **Packages and docs**: 38 modules per architecture (76 total), five LLVM23.1.2 rows and 33 driver-COMGR rows; rebuilt CPU hosts, refreshed RE9 source bundle, separate English/Chinese configuration references linked to all annotated defaults. Default files ship with MP1/PRED1/SKIN0; personal custom/native files are not included. The mirrors are available and the `0.41` tag is published; no GitHub Release page was created.

User observations on the installed development build: Stellar Blade 1x about 57.6 fps, fast 3x about 37 fps; Onimusha 900p fast 3x about 49 fps, unchanged and without anomalies. These are user observations, not controlled ABBA evidence of package speed-ups. gfx1200 receives build/ELF checks; hardware validation remains on gfx1201.


## Unreleased source update — 2026-10-06

- C32 normalization reuses its replicated per-lane inverse square root. The optional `c32-wave1-fast-norm900.hsaco` is selected with fast numerics on, processing size 1600×960, one real pass, graph off and experimental temporal history off. Other processing sizes and modes use their original modules. All 26 existing exports are checked together; a missing module or export falls back to the original module. No new user setting.
- On the RX 9070 XT, the original O2, timing-disabled HDR frame replay improved 900-tier mean time by 0.045–0.049 ms across three formal rounds; p99 improved in two rounds and was indistinguishable within the original 1 µs measurement resolution in the third. The 1152-row screen had a p99 regression and keeps its original route. These are frozen frame replays covering codec, bridge, network and decode, rather than full game frame rates.
- Standard 19 compatibility checks, controlled seed/history cases and module fallback checks were bit-identical. AE correctness was checked; performance was measured with AE off. This source update has not been installed or packaged. `Development/results/c32-norm-hoist-framework-20261006`, `c32-norm-hoist-stat-audit-20261006` and `c32-norm-hoist-compat-20261006`.

## Unreleased source updates — 2026-10-07–08

These changes are in source and locally tested builds; the published 0.41 packages remain unchanged. The temporary 0.41-a history experiment is not a new formal release.

- **Opt-in integration interfaces**: merged [TheAutomatic’s PR #12](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting/pull/12), adding optional codec controls and final-pass auxiliary outputs for history consumers. Existing codec defaults remain unchanged. The reusable auxiliary interface supports multiple passes; the temporal consumers below require one pass.
- **One temporal setting**, `DLSS5_TEMPORAL_MODE`, defaults to **0**:

  | Value | Behaviour |
  |---|---|
  | `0` | Off; no new temporal shaders or history resources |
  | `1` | TheAutomatic Fast History: model-gated blending with previous output, including feedback to the next network input |
  | `2` | Low-frequency temporal stabilization: blend only the low-frequency output correction; keep current high-frequency detail and do not feed it back into the network |

  Change it in `custom-config.txt` and **restart**. An explicit mode, including `0`, takes precedence over the legacy `DLSS5_FAST_HISTORY` key; the existing file/environment priority still applies to the new key. Invalid values are rejected. The older `DLSS5_TEMPORAL_HISTORY_EXPERIMENT` diagnostic cannot be combined with modes 1/2. `DLSS5_FAST_TEMPORAL` does not enable either new mode.
- **Mode 1 assets and runtime support**: regular HIP add-on and standalone RE9-style runtime now support Fast History. It needs the original 64-byte `post70-history-head.f16` row and matching C32 auxiliary exports, including the normal, RTZ, FAST and norm900 variants; changing a flag in an old package is insufficient. Missing assets fail explicitly. The runtime uses static screen-space history because its ABI does not supply verified motion/depth guides. The add-on uses motion/depth only with valid resources and explicit unjittered-MV/depth-direction declarations; otherwise it also falls back to static history with raw-colour change rejection. Unknown conventions are not guessed.
- **Mode 2 on AMD**: independently implemented from the algorithm described by [SAOG0721/Magpie](https://github.com/SAOG0721/Magpie/tree/2fceab5e241bc9f8ded001ab3266762f1f8bc51e). In the codec’s encoded colour domain, split the final correction into low/high frequencies, apply approximately 80 ms temporal memory to the low band, then preserve the current high band. It does not add another network pass, increase single-pass strength or reproduce two/three-pass enhancement. No Oklab controls or extra gain are automatically added.
- **Scope and recovery**: modes 1/2 require HIP, MP1, graph off and no overlap/external history; the add-on uses a full network viewport. ViT reuse is disabled for the temporal session without rewriting the user preference. Startup and F9/hot-reload guards reject MP2/3 while a temporal mode is active. Reset, exposure changes, long frame gaps and guide-mode changes invalidate history; the runtime retires constants/resources after actual queue submission. These modes are not enabled for the Magpie add-on by this integration.
- **Compatibility fixes**: restored the RGB input buffer’s first-use UAV-to-SRV transition; corrected MinGW wide-path file opening. Mode 0 preserves the previous path. Controlled 720/900 runtime checks with the new auxiliary modules were bit-identical when off; temporal shader, reset, gap and finite-output checks passed on WARP/RX 9070 XT. These checks do not prove improved game image quality.
- **Scoped submission optimization**: the unreleased `DLSS5_HIP_SUBMIT_PULSE=auto` source default uses one owned HIP event only on the verified gfx1201/Runtime7 driver and compatible single-pass, history-off recipes. Other scopes retain their prior path; `0` disables it. No output math or default quality setting changes, and no game FPS promise follows. The earlier norm900 optimization remains documented above.

Both temporal modes are approximations that change output and can produce ghosting. No bit-exact NGX equivalence, universal flicker fix, quality improvement or performance advantage is claimed. Local Stellar Blade/Onimusha trial installs use MP1; they do not change release defaults. Configuration details: [English guide](scripts/CONFIGURATION.md). Evidence: `Development/results/pr12-integration-20261007`, `low-frequency-temporal-20261007`, `lowfreq-runtime-20261007` and `mode1-assets-20261008`.
