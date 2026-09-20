# FP8 2:4 sparse FFN probe (AttExp)

Actual block31 expansion, M400/K1024/N4096. Keep the two largest absolute effective-FP8 weights per consecutive K4 group (stable tie order), zero the others. No input quantization changes or retraining. CPU quality uses the same11 transformed cases/64 valid tokens per case as the INT4 study; these derive from one capture, not independent scenes.

Run screen.py CAPTURE_ROOT WEIGHT_F16 OUTPUT_DATA with OPENBLAS_NUM_THREADS=2. Large generated operands remain outside git. common.py mirrors the INT4 reference FP8 and activation helpers. check.py OUTPUTS DATA REPORT_JSON checks the full400-token preactivation against the CPU pruned matmul and the final FP8 bytes against the CPU activation.

Sparse operand A is W, dense B is X; write the resulting transposed C into token-major output. Each lane owns8 compressed weight bytes,16 dense input bytes and16 meaningful index bits per K32. Fragment storage orders output16-block/K32/lane/bytes. N64 reuses dense input across4 sparse matrix instructions. Pack8 output bytes per lane into one8-byte store; scalar stores were much slower. FP32 preactivation diagnostics are disabled during timing.

Build bench.cpp using MinGW -O2 -std=c++17 -static -IDevelopment/HIP. Remote folder D:/DLSSNR-Lab/hip-backend/sparse-ffn-study contains bench.exe, kernel.hip, build.ps1, run.ps1 and generated fragments.fp8/fragment-indices.u32. build.ps1 uses existing dual-arch-src/rtc_compile.exe for gfx1200 and gfx1201. Runner uses the9070XT test machine/device0 and relative existing int4-gpu-data and vit-residual-adaptive-r3-modules assets. run.ps1 guards running games/Magpie and sets the working directory.

Benchmark compares existing R3 dense FP8 pack+FFN against the same input pack+new sparse FFN. Weight pruning/packing is offline. Two ABBA rounds,2000 warmup pairs/10000 measured pairs, host wall+stream sync; no graph. Each timed batch checks output bytes against its initial result outside timing. Archive output files before rerunning. Diagnostic pre.f32/sparse.fp8 are checked by check.py.

Final repeated savings40.6–41.5% for this isolated FFN expansion; pruning postactivation relativeL1 averages39.6%, not a final RGB percentage. This is speed potential only. No deployment/main change. Next: activation-aware/calibrated2:4 pruning, then isolated full-network RGB/temporal evaluation. Retained initial sparse_expand uses row-major compressed.fp8/indices.u32 and N16, useful as a mapping reference; current runner uses sparse_expand_frag.
