# GPU adaptive ViT reuse (AttExp only)

Matched experimental DLL/module ABI: **never put these guarded modules beside an ordinary release DLL**. Generated sources are under /tmp/vit-adaptive-src; production source and main remain unchanged.

## Mechanism

1. One wave per logical token compares block31 input with the last exact anchor, excluding Gather padding. Global relative L1 and worst-token relative L1 protect against large feature changes.
2. Exact32×32 means of the original working RGB input supply a second local test. Require a coherent2×2 neighborhood of changed tiles, so an isolated moving dark edge does not veto every pan. This is a64×64-scale heuristic, not a guarantee for smaller changes.
3. One wave decides;53 ViT exported kernels have an additional optional device gate and immediately return on a reused frame. Host submits the same launches. No per-frame CPU decision readback.
4. Finish selects `anchor_output + gain*(input-anchor_input)` rounded to the model lattice, or exact output. Only exact outputs refresh feature and image anchors.

Provisional thresholds: feature global0.22, feature local1.0, coherent image0.35; feature token denominator floor16, image RGB-sum floor1e-6. Max period4. Initial state, geometry changes, >500ms processing gaps, input allocation/seed/history transitions, and re-enabling after exact mode invalidate anchors. Graph off, fast pooled pipeline, byte-stream ViT off, no skipped blocks31–38; existing decoder skips42/43/46 remain allowed.

Mode `DLSS5_VIT_ADAPTIVE`:0 full baseline (REUSE_PERIOD is ignored);1 adaptive;2 forced full through wrapper;3 forced GPU schedule. These and gain path/thresholds are experiment-only flags. Diagnostic gate logging synchronizes and is not enabled for timing or the preview.

## Reproduction

- build-host.sh generates source and benchmark_vit_adaptive.exe.
- Upload kernel/deep_fast.hip as hip-backend/vit-residual-adaptive.hip; residual/build.ps1 -Suffix -adaptive builds gfx1200/gfx1201 and assembles flat gfx1201 benchmark modules.
- run.ps1 Control:900/1080 baseline, disabled, forced-full, forced GPU schedule and adaptive frozen checks.
- suite.ps1 Quality: seven controlled sequences at900. Sequences1–4 follow residual tests;5 vertical2px/frame plus exposure;6 a1%-area dark patch appearing/disappearing;7 repeated mirrored cuts.
- suite.ps1 Timing:160-frame ABBA on static/shift/combined motion at900/1080, exclude32 warmup frames, use means. All12 input frames are precomputed so expensive CPU half conversion does not starve the GPU between samples. Complete collection/compression/transfers before timing.
- temporal-check.ps1: force-full bitwise reference and adaptive finite/reset behavior with history enabled, reset every4 frames. Zero motion vectors are synthetic, not gameplay validation.
- build-toggle-test.sh + toggle-check.ps1 test off/on mode transitions. Physical F8 key delivery is still an in-game check.

RGB captures are1296×720, network tier900/1080; all controlled sequences derive from one capture. They are not gameplay video. First-frame cold initialization can cause one further500ms timeout/full refresh.

## Preview for Stellar Blade

build-preview.sh MINHOOK_SOURCE RESHADE_INCLUDE builds /tmp/native-vit-adaptive.addon64 from the same generated host. stage.ps1 assembles50 hash-listed files at `D:\DLSSNR-Lab\AttExp-preview`: DLL, original-weight gain,48 modules for two architectures. No game writes during staging.

`install-preview.ps1 -Action Install|Restore|Status` defaults to StellarBlade/SB/Binaries/Win64. Install and restore reject a running game, back up every replaced file and configuration, verify hashes and restore on failure. test-install.ps1 exercises install/restore in an isolated fake game directory; no real game is used for that test.

After installation: **F8 toggles adaptive/exact computation**, leaving DLSS5 active. F6 keeps its normal whole-effect toggle. If the existing DLSS5 FPS row is enabled, it appends AE or EXACT; its normal3-second refresh and visibility controls remain in force. Mode defaults to adaptive on a new network instance. Start by checking camera pans, occluding characters, menus, scene changes and F8 A/B. The actual game installation remains a separate step.
