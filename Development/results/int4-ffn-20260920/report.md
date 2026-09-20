# Actual block31 INT4 feasibility — 2026-09-20

AttExp experiment only. No INT4 DLL/module installed into Stellar Blade. R3 remains in use. GPU tests ran only after verifying game/Magpie processes absent.

## Target and numerical contract

Original block31 expansion weight:4096×1024 binary16,8MiB, SHA256 ee20133146ae0410282390f8883451d0975adfcf95d02f0e2e671a92a97d396a. Compare with the current effective FP8 matrix/input, not an FP16 matmul strawman. Actual captured input is400×1024, already exactly on the FP8 lattice. Retain the original clamp/polynomial activation and E4M3 output. This is one FFN **expansion operator**, not the whole FFN or network.

CPU calibration uses375 real tokens from frozen frame0, with64 tokens per evaluation frame from11 frozen/shift/exposure/cut/occlusion inputs. All derive from one image; this is not independent cross-game validation. The CPU FP32 matmul is a screening reference, not bit-exact WMMA accumulation. Low-rank8/16/32 is an original randomized spectral approximation with simple channel balancing, not the DeepCompressor calibration implementation.

## CPU screen: post-activation relative L1

| Representation | Error |
|---|---:|
|plain INT4 row|32.60%|
|plain INT4 group64|20.33%|
|rotation64 + row INT4|20.56%|
|rotation64 + group64 INT4|15.43%|
|channel balance + group64|15.57%|
|balance + rotation64 + row|17.93%|
|balance + rotation64 + group64|13.61%|
|balance + rotation + group64 + rank8|13.22%|
|same + rank16|12.94%|
|same + rank32|12.60%|

These are **intermediate activation errors**, not perceptual-quality percentages. The original decoder/RGB has not been run with these candidates. In this approximate decomposition, rank8/16/32 captured about5.24%/8.96%/15.37% of balanced-weight squared energy; low-rank correction gave only incremental error reduction here and adds work. This does not rule out a better calibrated SVDQuant representation.

An additional rowwise clipping calibration selected0.75 using only the frozen-frame post-activation RMSE, then evaluated other transformations: about20.46% mean relative L1. It is faster to preprocess than rotation in principle, but less accurate in this screen.

## GPU implementation and validation

Native gfx12 IU4 WMMA, signed4-bit operands with INT32 accumulation. Packed fragments match the instruction's16×32 by32×16 operands. Both gfx1200/gfx1201 compile; executed on9070XT/gfx1201 only.

Candidates include direct row INT4, balanced/rotated row INT4, balanced/rotated group64 INT4, and balanced/clipped row INT4. The grouped variant also tries N32 instead of N64 output tiles. Inputs are quantized on GPU every call; no hidden CPU-prepared activation is used for timing. Offline weight conversion is excluded for both FP8 and INT4. Low-rank correction is CPU-screened only and is not included in the GPU speed measurements.

Initial GPU quantization matched CPU exactly; matrix pre-activation matched CPU multiplication of the actual quantized operands at relative RMSE about1.6e-7–2.1e-7. Tuning precomputes inverse channel scales, replaces repeated division with a shared reciprocal, specializes row/group GEMM, and tests a single-wave row quantizer without a workgroup barrier. The fast reciprocal can change clipping-boundary rounding; final reports compare actual GPU-packed operands as well as CPU quantization. This is not claimed bit-exact to a different quantizer.

## Timing interpretation

Baseline is the actual R3 vit_pack_input plus fragment-layout FP8 expansion. All candidate pipeline times include input transform/quantization, matmul, scale restoration, activation and output quantization.

Ordinary synchronized10000-pair batches showed unstable rowwise timings and no stable end-to-end win. Group64 was more stable but also slower. Separate component times were not additive (notably grouped GEMM-only could be much slower than the combined pipeline), so they cannot establish an exact cost decomposition.

128-pair HIP graph batching initially showed favorable row/rotation throughput, including one ABBA about35.58→30.34 microseconds (14.72% shorter). Subsequent alternating runs did not reproduce that advantage. The early result is retained under pre-n32; it is **not an achieved optimization**. Single-exec graph throughput is also not the current adaptive game submission mode. Kernel arithmetic checks and post-batch output checks passed, but the timing sensitivity to submission conditions remains unresolved. No driver root cause is asserted.

## Decision and next controlled experiment

Do not deploy these prototypes as an accelerated game build. Quantization itself works, but current cost/quality evidence is insufficient to select a replacement for FP8.

Next: place the candidate at the original expansion's location in an isolated full-network replay, initially execute it without consuming its result, and compare added cost against an extra original FP8 expansion under the same submission/working-set conditions. Then separately consume the candidate output to measure the decoded RGB and temporal error. Optimize producer-side quantization/fusion only after that test identifies a worthwhile path. Keep original activation/rounding boundaries and account for scales, low-rank work and extra memory traffic.

Source inspiration: comfy-kitchen HIP [SVDQuant](https://github.com/0xDELUXA/comfy-kitchen_win-rocm/blob/d9630b8c6524c791671138520c84afa358ef08b3/comfy_kitchen/backends/hip/ops/svdquant_w4a4.hip), [ConvRot](https://github.com/0xDELUXA/comfy-kitchen_win-rocm/blob/d9630b8c6524c791671138520c84afa358ef08b3/comfy_kitchen/backends/hip/ops/convrot_w4a4.hip). Prototype kernels here are standalone implementations for our actual operator; the library is not installed in the game.

## Final checked ordinary-batch results

| Candidate | FP8 pair μs | INT4 pair μs | Added time | GPU post-activation relative L1 |
|---|---:|---:|---:|---:|
|plain row|34.768|43.558|25.28%|32.39%|
|balanced + rotation / row|35.404|47.499|34.16%|17.84%|
|balanced + rotation / group64,N64 tile|35.572|37.303|4.87%|13.58%|
|balanced + calibrated clip / row|35.482|46.118|29.98%|20.52%|
|balanced + rotation / group64,N32 tile|35.594|39.216|10.17%|13.58%|

Rowwise candidate times vary substantially between paired batches, so these means are not stable estimates of per-frame latency. No candidate passes a stable speedup criterion. Final graph ABBA also fails to confirm the early positive graph result; all CSVs are retained. N32/N64 grouped outputs are byte-identical. All ordinary/graph output checks passed after their timed batches.

Final source SHA256 fe61614236f3a07a0a489488d6dfcdf92815ac229507e44adb6382d13c64a2cc; gfx1200 HSACO9c924fde3f7278385ac7a7588b4ef26673fb00599cef5d26ff379c3e2aebe02e; gfx1201b670c1662d33008b7cb6ebf267ce5e70ac0345cc5d4f43bbdc35bdbd3f6b7391. First-prototype results are under initial; the earlier mixed graph result under pre-n32; final holds the checked latest runs. Large data and calibration binaries remain in /tmp and the Windows lab, not in git.
