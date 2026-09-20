# Adaptive ViT reuse — 2026-09-20, AttExp

Two rounds on the exact-stream attention baseline. Experimental matched DLL/HSACO ABI; original weights, production sources, main and game installations unchanged. RX9070XT runtime; gfx1200/gfx1201 compiled. Graph off.

## Round1: feature gate on GPU

One wave per token computes relative L1 against an actual block31 input anchor. A second wave combines global delta and worst-token delta;53 ViT entry points have optional device guards and skip arithmetic on reuse frames. A finish kernel applies the original-weight gain predictor or updates anchors from exact results. CPU submits all kernels without reading a decision back. Maximum period4.

With global0.20/local1.0, static900/1080 disabled/full/fixed/adaptive controls match exactly. Seven900 sequences compare every frame: every recomputed frame is bitwise equal to its reference. Mirror cut, large occlusion and repeated cuts are completely exact. The1%-area dark patch leaks one reused frame (display MAE1.191/255), motivating round2.

R1 ABBA frozen time13.27395→12.32143ms900 (7.18%),18.74621→17.09418ms1080 (8.81%); horizontal1px motion13.07988→12.21750 (6.59%),18.33538→16.76741 (8.55%). The combined vertical-motion/exposure sequence had0.376/0.684ms baseline drift, exceeding its small apparent savings; no positive claim from that row. R1 generates exposure frames on CPU between timed calls, potentially starving/clocking down the GPU. R2 precomputes all12 frames once and only copies each selected frame between calls. R1/R2 timing should not be subtracted to isolate gate overhead.

## Round2: coherent image-region gate and runtime controls

A first32-sample-per-tile image check failed twice: denominator floor hid the dark patch; sparse sampling falsely rejected all horizontal motion. Full32×32 means fixed the missed patch but worst-single-tile ratios still rejected moving dark edges. The retained method takes the minimum ratio within each2×2 tile neighborhood, then its maximum over the image. This looks for a coherent64×64-scale change. Dark denominator floor1e-6; image threshold0.35. Bad/nonfinite samples always reject reuse independently of neighborhood consistency.

History-enabled input changed slightly more than the non-temporal sample: global0.20 rejected every test frame. The final preview uses0.22, preserving local1.0 and coherent-image0.35; these are tuned heuristics, not error guarantees. All sequences come from one captured frame and were used during development. They are not an independent gameplay dataset.

Mode0 always runs full computation even when period4 remains set. F8 toggles adaptive/full in the preview; existing FPS display, if enabled, appends AE/EXACT with its normal3-second refresh. Resume, >500ms gaps, input/seed/history transitions and re-enabling invalidate anchors. No diagnostic gate readbacks in the preview.

## Validation and artifacts

- Static900/1080 full/fixed/adaptive checks pass; exact-stream output preserved on full frames.
- History force-full:12 frames with reset every4 match bitwise. Adaptive history run is finite, and history reset/transition frames refresh. Synthetic zero motion vectors are not actual-game temporal validation.
- Mode transition test: frames4–7 disabled while period4 remains set match baseline; re-enable8 refreshes and matches. Physical F8 delivery/overlay appearance still needs in-game verification.
- Isolated install/restore test verifies50 payload files, original configuration, original files and directory topology. Newly created empty architecture directories are removed on restore, since leaving them would override a previously flat module layout.
- Latest DLL SHA256 `6b544f3d17b9ac20ef725be748379db7418521b7f8c717e839ed2170a55dc69e`.
- Deep module gfx1200 `c446125b152930b3fd1e838599875d241617c2185fb6ae970931500215544c8a`; gfx1201 `ac99c196425e4aa65db4fa89d2e1a4b136012759baab2c6fc7abc055f3f393b7`.
- Staged at `D:\DLSSNR-Lab\AttExp-preview`; install-preview.ps1 handles Install/Restore/Status. No real game files overwritten. This is a local preview, not a public release package.

Raw captures remain on the Windows lab / local temporary data directories. JSON reports keep per-frame results and complete timing run summaries. Final R2 quality/timing results follow below.

## Final R2 quality (global0.22/local1.0/coherent image0.35)

900 network tier,12 frames per sequence; display MAE in0..255 after clipped sRGB conversion, averaged over **all** frames. Every full frame in the no-history run is bitwise equal to its exact reference.

| Controlled sequence | Reused frames | Mean MAE | Worst-frame MAE |
|---|---:|---:|---:|
|1px horizontal shift|7/12|0.721|1.271|
|exposure +1%/frame|8/12|0.829|1.264|
|mirror cut|7/12|0|0|
|large dark occlusion|7/12|0|0|
|vertical shift + exposure|3/12|0.326|1.398|
|1%-area dark patch|7/12|0|0|
|repeated mirrored cuts|7/12|0|0|

The small patch now forces refresh at appearance frame4 and disappearance frame9. In the synthetic history-enabled shift/reset test,4/12 frames reuse; mean MAE0.202, worst0.520. Full frames in a temporal run may differ because previous approximate output was fed into history; the strict full-frame identity check applies to no-history runs, while forced-full history control matches throughout. Smaller/subtile changes can still escape image gating. Subjective quality and actual motion vectors remain for gameplay testing.

## Final R2 timing

160-frame ABBA per case, first32 excluded; full ProcessSubmittedFrame mean including decision/commit kernels, bridge and postprocessing. Graph off; no diagnostic dumps, compression or transfers during timing. Controlled input prepared before the loop. Reference is exact-stream, not old production; no game rendering/upscaler/frame-generation cost included.

| Tier | Input/history | Baseline ms | Candidate ms | Saved ms | Reduction | Baseline drift ms |
|---|---|---:|---:|---:|---:|---:|
|900|frozen / no history|13.24228|12.35397|0.88830|6.71%|0.03007|
|900|horizontal shift / no history|13.20843|12.44792|0.76050|5.76%|0.02459|
|900|vertical shift + exposure / no history|13.22533|13.00567|0.21966|1.66%|0.00566|
|1080|frozen / no history|18.75687|17.23364|1.52323|8.12%|0.01687|
|1080|horizontal shift / no history|18.73945|17.40502|1.33443|7.12%|0.03658|
|1080|vertical shift + exposure / no history|18.77112|18.56855|0.20257|1.08%|0.01783|
|900|horizontal shift / history|13.45100|12.77562|0.67539|5.02%|0.01923|
|1080|horizontal shift / history|19.26721|18.08421|1.18301|6.14%|0.00124|

All eight measured cases improved; none establishes a guarantee for other scenes. Larger-change inputs trigger more full evaluations and have smaller gains. The history timing run has no periodic resets; the separate12-frame history quality test resets every4 frames, so their reuse percentages are not interchangeable. Final staged gfx1201 modules match all24 benchmark module hashes; manifest.json records the complete50-file preview. Install/reinstall/restore tested in an isolated directory, retaining the original backup on duplicate installs.
