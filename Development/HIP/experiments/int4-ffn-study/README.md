# INT4 FFN feasibility study (AttExp)

One actual block31 expansion,4096 output ×1024 input,400 tokens. Targets the current effective FP8 operands, including the original clamp/polynomial activation and E4M3 output. No game DLL/module changes.

CPU: `screen.py CAPTURE_ROOT WEIGHT_F16 RESULTS` compares row/group64 INT4, orthogonal64-channel transforms, calibration-based channel balancing, and randomized low-rank8/16/32 residual corrections. This is an original screening approximation, not the DeepCompressor/SVDQuant calibration pipeline. `calibrate_row.py` selects a common row clipping ratio using only frozen frame0; other transformed frames are evaluation, but all derive from one capture. Limit BLAS/OMP threads to2. CPU matmul ordering differs from GPU WMMA; these are layer errors, not RGB or frame-rate measurements.

`screen.py` writes its generated parameters to /tmp/int4-block31-calibration.npz. `export.py CAPTURE_ROOT WEIGHT_F16 DATA_DIR ROW_CALIBRATION_JSON` emits test operands, original FP8 fragments and packed INT4 fragments. Large/generated weights/calibration stay outside git.

GPU: build bench.cpp with MinGW and -IDevelopment/HIP; build.ps1 compiles int4.hip for both gfx1200/gfx1201. run.ps1 rejects running games/Magpie and invokes the isolated9070XT/gfx1201 probe. Existing R3 `vit_pack_input` + `vit_expand_blocked_fp8_frag_bytein` are the baseline. Candidate pipelines include input scaling/rotation/quantization + signed-INT4 WMMA + rescaling + original activation/output quantization. Offline weight conversion is excluded on both sides. Low-rank correction is not implemented in this GPU probe, so no low-rank speedup is claimed.

The tuned kernels precompute inverse channel scales, reuse a reciprocal quantization scale, and specialize row/group accumulation. Rowwise INT4 uses a single INT32 accumulator across K; group64 rescales each64-input partial into FP32. These have different resource costs.

Benchmark uses host wall time over synchronized batches (2000 warmup pairs,10000 measured pairs), not HIP event timestamps. Hot weights and isolated repeated work are not full-network timing. `check_gpu.py GPU_OUTPUT DATA_DIR REPORT_DIR` checks the FP8 control against CPU, validates GPU INT4 pre-activation against a CPU product of the actual GPU-packed operands, checks the quantizer against CPU, and reports layer error and ABBA timings. Use a separate directory for initial/tuned measurements.
