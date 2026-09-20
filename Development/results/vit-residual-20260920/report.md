# ViT whole-section reuse — 2026-09-20, AttExp

Isolated block31–38 experiment based on exact-stream attention. Original weights and production source unchanged. gfx1200/gfx1201 compile; execution on RX9070XT/gfx1201. Graph off, no temporal history, seed0. No game deployment.

## Predictor

Naive `x+(y_anchor-x_anchor)` assumes identity skip. Original16 contract/projection skip vectors instead have product gain min0.0000406368, max0.7285523, mean0.0672899. New prediction `y_anchor + gain*(x-x_anchor)` follows only that direct path; nonlinear branches and intermediate rounding remain unmodelled. Actual anchors only, no recursive prediction. Equal components copy; changed components round onto model lattice.

Frozen900/1080 controls for disabled, period2 and period4 match baseline bitwise. Forty-eight unit-reuse pre-ViT inputs match baseline. Both unit/gain dynamic variants refresh frames0/4/8 exactly; all outputs finite. Gain pre-ViT feature files remain on Windows and were not separately downloaded/compared.

## Quality: four-frame forced schedule

900 network tier, twelve1296×720 frames per controlled sequence, not gameplay footage. MAE is clipped linear RGB converted to sRGB, expressed on0..255. Reused-frame means exclude exact refresh frames.

| Sequence | Unit MAE | Gain MAE | Gain worst-frame MAE |
|---|---:|---:|---:|
| 1px/frame shift |1.286|1.260|1.296|
| +1%/frame exposure |1.252|1.232|1.253|
| mirror cut |0.668|0.649|2.921|
| local black occlusion |0.487|0.483|2.172|

Cut/occlusion occur at frame6; errors persist at6/7 until refresh8. Their means are diluted by unchanged frames: use worst/change frames too. Gain cut-frame p99 error21.47/255; occlusion16.45/255, maxima95.87/115.45. No claim of imperceptibility. Feature relative L1 improved more (shift29.1→17.7%), but this did not translate into proportional RGB improvement. See actual GPU rgb JSON and offline feature proxy JSON separately.

## Timing

Clean160-frame ABBA, first32 excluded,128-frame **means**, including32 full+96 reuse frames. Frozen input with equal-component copy branch; this is scheduled reuse opportunity, not dynamic/adaptive gameplay timing. First exploratory timing overlapped compression/transfer and was discarded; final run had neither.

| Tier | Full baseline | Period4 gain | Saved | Time reduction |
|---|---:|---:|---:|---:|
|900|13.24957ms|12.25709ms|0.99248ms|7.49%|
|1080|18.76698ms|17.06921ms|1.69777ms|9.05%|

Baseline start/end drift0.02206/0.00044ms. Timing includes wrapper, actual refresh and GPU reuse application; no diagnostic dumps. Runtime decisions/scene-cut detector do not exist yet and would add cost. Do not translate these into game FPS gains.

## Next

A larger win than small K/V merges, but not a route to2× by itself. Build a cheap GPU-local change statistic including per-token tails/local maxima, then test conservative full refresh on scene changes and localized occlusion. Avoid CPU readback per-frame; evaluate decision overhead and dynamic inputs. Calibrate on separate sequences/captures, not the four tests used to invent a threshold. Real motion and temporal stability need gameplay validation before deployment. Fixed period4 remains an experiment, never default.

Gain modules SHA256: gfx1200 0ded2af31d40d5f62eae16409fc0bf160f3526cde8e167797f5917689fad7913; gfx1201 00fe75bcf81a5ffab6bc56e896248c2b4ed14e81741ae8ba01d3e3fe7479c282.
