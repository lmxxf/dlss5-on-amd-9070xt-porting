# Cached K64 groups — 2026-09-20

Candidate `int4_expand_group_cached` keeps N64 output tiles and explicitly executes the two K32 integer MMAs belonging to each quantization group. It loads the eight activation scales once per group, shares them across four output fragments, and uses simpler base-plus-offset weight addressing. Floating-point rescaling order remains `(float(acc) * input_scale) * weight_scale`, then addition in original K64 order; no new approximation.

Both gfx1200/gfx1201 compiled; gfx1201 executed on RX9070XT. The existing complete pipeline (input rotation/scaling/quantization + GEMM + original activation/FP8 output) is timed. Same 400-token block31 input/weights as previous studies. No game/Magpie running before/after. Ordinary host wall batches, 2000 warmup/10000 measured pairs, no graphs.

First run interspersed FP8 controls: candidate about60–62µs while original grouped INT4 batches about63–73µs; FP8 about35.6µs. This suggested an INT4 improvement but did not establish matched direct speedup. Second run used original INT4 as the direct control, including identical-control batches before/after:

| Direct ABBA | Original INT4 | Cached groups | Reduction |
|---|---:|---:|---:|
| 1 |71.422µs|67.389µs|5.65%|
| 2 |72.140µs|66.665µs|7.59%|

Both runs compare method3 (original INT4) vs method5 (candidate): packed input, scales, FP32 preactivation and FP8 output all byte-identical. Every timed batch also checked against its initial output. These are fixed-input arithmetic checks, not broad scene/geometry coverage. Raw CSV retains all slots, including drift in identical-control runs. Summary first-run percentages are versus FP8 and are negative; direct-run percentages are versus original INT4. This is a retained experimental improvement, not INT4 beating FP8 or a game FPS gain. Existing submission sensitivity remains unresolved.

Module SHA256: gfx1200 fc74c542f3a2a263c728db9947b34a9f6d5c72ae659d91298c9f5dabe29fd314; gfx1201 49593ac8976221fbd407be62264fa38c4b05e6e048eb93b8489cb1400e88feb9.

Reproduce with bench-tile.cpp, `-DINT4_TILE=64 -DINT4_KERNEL='"int4_expand_group_cached"'`, usual MinGW C++17 static build and HIP include path. Add `-DINT4_DIRECT` for the direct-control run. Save as remote experiment bench-cached.exe and use run-cached.ps1; archive cached-out between runs. Method5 selects cached groups; method3 remains original N64. Original kernels/defaults are unchanged. Game stays R3. Carry candidate into the planned full-network insertion experiment before adoption.
