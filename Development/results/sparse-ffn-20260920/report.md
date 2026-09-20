# FP8 2:4 sparse block31 expansion — 2026-09-20

Requested large FFN test: actual4096×1024 weights,400 tokens. Original effectiveFP8 input/weights and activation are the reference. Magnitude2:4 pruning retains two entries per K4 and keeps FP8 precision. CPU11-case mean postactivation relativeL1 is39.5999%; this is a layer error, not RGB quality. No calibration or fine-tuning yet. Data comes from one captured image with controlled transformations.

Native gfx12 sparse instruction compiled for gfx1200/gfx1201 and executed on RX9070XT. A holds compressed weights, B dense inputs; sparse output is transposed into network layout. CPU full400-token pruned-matrix result and GPU preactivation compare exactly on this sample, as do final activation/FP8 outputs. All final timed batches preserve their reference output. This verifies the implementation against the pruned matrix, not equivalence to original weights.

| Implementation stage | Dense FP8 | Sparse FP8 |
|---|---:|---:|
| Initial N16 row-major |~34µs|~122µs|
| N64 with coalesced weight fragments |~35µs|~53µs|
| Packed8-byte output, independent repeat round1 |34.439µs|20.446µs|
| Packed8-byte output, independent repeat round2 |34.757µs|20.335µs|

Final independent repeat saves40.63%/41.49% (~1.68–1.71x throughput). Initial packed run also around20µs. Full raw ABBA CSVs and timing-summary.json retained. Includes original FP32→FP8 input packing, expansion, activation and output; offline weight processing excluded. Timings use synchronized host wall with2000 warmup/10000 pairs, no graph. Fixed hot working set; do not convert to game FPS or whole-network speedup. GPU processes guarded absent before/after.

Final module SHA256: gfx1200 0b392ac756e84f11ebf9d3b1c59db1fca685d71e25ceb7722769db7bd7ae1b64; gfx1201 4d46c053907d5db9a5b682d0ee341afb67a5cd2887d627ce9e61a3f76dfcc01f. Prior N64 scalar-store module gfx1201 699dd36c804c839a7972152842aa80ae23592743bff9e0d6453a1c6d184ef1a4. Initial N16 gfx1201 2de97d1bc09174661b0dc439221163cd3b0c2870ce95fb30adc1398a4ad4378f.

Decision: worthwhile speed route, quality unresolved. Next test activation-aware/calibrated masks to lower distortion before full-network RGB/temporal assessment. No game installation or main migration. Current game stays R3.

Sources checked: AMD RDNA4 ISA https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/rdna4-instruction-set-architecture.pdf ; LLVM builtin signature https://clang.llvm.org/docs/AMDGPUBuiltinReference.html (`__builtin_amdgcn_swmmac_f32_16x16x32_fp8_fp8_w32`, int2/int4/float8/index). Actual local HIP7 COMGR compilation and GPU oracle validation establish availability/layout on this environment.
