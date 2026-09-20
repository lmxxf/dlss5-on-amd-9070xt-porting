# ViT residual reuse experiment (AttExp only)

Wrap the gathered block31–38 section. A real evaluation records anchor input/output; a forced reused frame predicts `current + (anchor_output - anchor_input)`, rounds to the existing FP16/FP8 output lattice, and never promotes predictions into anchors. Equal input components reuse the anchor output directly, giving a static control. Geometry mismatch forces a real evaluation. This is not an adaptive policy or game deployment.

`build-host.sh` generates an isolated host under `/tmp/vit-residual-src` and the module source based on the bit-matching exact-stream attention. Upload kernel/deep_fast.hip as `D:\DLSSNR-Lab\hip-backend\vit-residual.hip` and the EXE as benchmark_vit_residual.exe; build.ps1 builds both architectures and assembles separate vit-residual-modules. run.ps1 phases: Control; Capture (900 default); Forced (period4 default); Timing. Graph off; guard against running games.

Sequences: 0 frozen, 1 one-pixel right translation per frame with wrap-around, 2 +1% exposure per frame, 3 sudden horizontal mirror halfway, 4 a black local occlusion halfway. All are controlled inputs derived from one captured HDR frame, not a real gameplay video. Diagnostic dumping/synchronization is excluded from performance tests.

Capture writes pre/post ViT logical token arrays as float32, before inverse Gather. Features are in the model's Gather ordering, not raster order. RGB output files remain1296×720 RGBA16F. The network tier determines400/640 tokens. Runtime flags are DLSS5_VIT_REUSE_PERIOD, DLSS5_VIT_RESIDUAL_DUMP/LOG, DLSS5_RESIDUAL_SEQUENCE/RGB; experimental only. Period0 is baseline, periods2/4 etc force scheduled reuse without checking scene changes and must not ship.
