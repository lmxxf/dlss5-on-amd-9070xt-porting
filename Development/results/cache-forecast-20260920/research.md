# Repository follow-up and causal predictor screening — 2026-09-20 18:57

Research only on AttExp. Game feedback:900P52–54FPS, little perceived change; no matched F8 pair was supplied. No game files changed or GPU benchmark run this round. CPU study uses existing captured ViT features, not new gameplay capture.

## Source snapshots and what is actually implemented

Remote refs checked: comfy HIP Sol d9630b8c6524c791671138520c84afa358ef08b3; token augmentation b99d2207b0865a3cc660ffb8265eadde3f891ead; WMMA31e4bfef6d67e0cf141d7d2751baea32ff4f6d7d; GEMM tile selection9192c77751175c67ee76c7ef7ebb1327642320d9. Spectrum HEAD5161f0457bc8c52535212d6783eee73f439e1537 and Sol Triton HEAD26d816ebd4f1e43a2c6e4d4759be3137f10a7a73 match the earlier audit: these are newly examined details, not newly published changes.

### Comfy HIP: new priority, four-bit GEMM beyond attention

[svdquant_w4a4.hip](https://github.com/0xDELUXA/comfy-kitchen_win-rocm/blob/d9630b8c6524c791671138520c84afa358ef08b3/comfy_kitchen/backends/hip/ops/svdquant_w4a4.hip) contains actual HIP quantization, low-rank down-projection and INT4 GEMM launchers. It applies per-K64 activation/weight scales, rescales INT32 partial accumulators per group, and adds the low-rank up-projection in the GEMM writeback. The down-projection and activation quantization are separate kernels: a single high-level call is not one GPU launch.

The operating idea is `XW^T ≈ Q4(X/s) Q4((W-LR)*s)^T + (X R^T)L^T`, with scales restored. Preserve a small high-precision branch for directions that four-bit quantization handles poorly. The original network remains the target; quantization/calibration changes its numerical approximation.

The tensor layout's `quantize()` explicitly raises NotImplementedError and requires offline DeepCompressor calibration/pre-quantized state. Thus this repository supplies inference machinery, not a ready converter for our weights. Need our own extraction/calibration/packer and a representative activation set.

[convrot_w4a4.hip](https://github.com/0xDELUXA/comfy-kitchen_win-rocm/blob/d9630b8c6524c791671138520c84afa358ef08b3/comfy_kitchen/backends/hip/ops/convrot_w4a4.hip) and eager/convrot_w4a4.py provide an alternative: matched orthogonal channel transforms before quantization. In exact arithmetic, orthogonal H preserves a dot product when applied consistently; quantization after transformation is lossy. Bias, original output rounding and nonlinear activation stay at their original boundaries.

mma.h uses gfx12 INT4 `16x16x32`, FP8/INT8 `16x16x16`; this is a concrete wider-K instruction opportunity, **not a measured2× kernel/frame speedup**. Packing, rotation/smoothing, per-group rescaling, extra low-rank work, register pressure and lost fusion all count. C32's short K also fits this scheme less naturally than K256/1024/4096 GEMMs.

First isolated operator: ViT block31 FFN expansion (existing captured input, K1024/N4096, natural64-element groups). Compare effective current FP8 operands with W4A4, rotation and rank8/16 correction at the activation output; then measure the entire quantize+correction+GEMM path on GPU. If useful, investigate C256 fused FFN where more runtime is at stake, accounting for the existing fusion instead of replacing it with a slower generic stack.

### Spectrum: history weights and observed error feedback

[forecast.py](https://github.com/xmarre/ComfyUI-Spectrum-MiniMax-H3/blob/5161f0457bc8c52535212d6783eee73f439e1537/comfyui_spectrum_h3/forecast.py) solves a small regularized system to obtain weights on actual history snapshots. It does not require a full feature-sized coefficient matrix. generic_correction.py/runtime.py use actual-anchor error projected onto a historical direction, signed bounded gains and coordinate transport. GENERIC_CORRECTION_RESEARCH.md explicitly retains negative gains and separates hidden-space error from decoded-media validation; model-aware feature3 was retired for lack of material gain.

Only the causal single-pass ideas transfer to online game frames. The repository's offline smoothing/replay and diffusion-solver-specific correction are not a realtime-game drop-in. We have no equivalent diffusion sigma coordinate; arbitrary camera motion need not be a smooth time polynomial. A sampled final linear-head metric also cannot be transplanted across our entire nonlinear decoder from the ViT boundary.

### TeaCache: calibrated risk and a larger cached function

[FLUX path](https://github.com/welltop-cn/ComfyUI-TeaCache/blob/91dff8e31684ca70a5fda309611484402d8fa192/nodes.py) measures consecutive first-block modulated-input differences, applies model-specific polynomial calibration, accumulates that risk, and reuses a block-stack residual. Our R3 instead compares against the last exact anchor with fixed thresholds and an age cap. A calibration of actual prediction error at recomputed frames is still missing; copying their coefficients would be invalid.

The larger opportunity is a local block/window cache covering FFN as well as attention. Measure one ordinary C32 block's actual inputs and all outputs first; its raw/main intermediate layouts, shifted-window halo and scaled skip paths must be preserved. Do not cache a resized final RGB or use identity-residual assumptions again. The old profiling table gives C32-family marginal duplication costs3.198/4.704ms900/1080 and ViT1.549/2.972ms, which prioritizes investigation but is not an additive decomposition or guaranteed saving.

### Sol/token augmentation: query-dependent exact work

[HIP token routing](https://github.com/0xDELUXA/comfy-kitchen_win-rocm/blob/b99d2207b0865a3cc660ffb8265eadde3f891ead/comfy_kitchen/backends/hip/sage_attention/sol_attn_token.hip) uses grouped query centroids to score tokens in otherwise-unrouted blocks, a bounded histogram selection and deterministic sorting; selected contributions leave the pooled tail before exact evaluation. [Sol Triton](https://github.com/kijai/ComfyUI-SolAttn_triton/blob/26d816ebd4f1e43a2c6e4d4759be3137f10a7a73/_tri_fwd.py) likewise routes by query-dependent scores. Our prior K/V similarity merging is not this full method.

A DLSS experiment could select exact query/key tiles based on current Q plus a cheap error/importance estimate, retaining a representative tail and its correct denominator. But our400/640-token ViT and64-token local windows are much shorter than the source's intended long-sequence workloads. Producer/histogram/sort/list/quantization costs can erase savings. Preserve our nonstandard exponent and FP8 rounding contracts in the baseline; standard-softmax centering equivalences are not automatically bit-exact here. Sage-style operand packing has already helped; INT8 conversion alone is not an unclaimed automatic speedup over our existing FP8 path.

## CPU pilot: historical residual prediction

Own small adaptation inspired by Spectrum, **not a reproduction of its full algorithm**. Define `r_t = y_t - g*x_t`, where g is our original-weight skip-path gain. Start from R3 hold `y_a + g*(x_t-x_a)`; compare linear/damped residual extrapolation, equal averaging of two actual residual anchors, and a bounded coefficient fitted only when a new actual anchor arrives. Original equal-component bypass and FP16→FP8 lattice approximation are retained. NumPy does not reproduce GPU FMA/WMMA bit behavior.

Five existing12-frame900-tier sequences, no temporal history. Force actual anchors0/4/8, never promote predicted anchors. Summary compares the six reused frames5/6/7/9/10/11 where two anchors exist. Fitting occurs only at actual frame8 using anchors0/4, then applies to9/10/11; there is no access to future target y during prediction. The fixed schedule deliberately does not apply R3's scene-change gate.

| Input | R3 gain hold | linear extrapolation | damped0.25 | causal bounded fit | two-anchor mean |
|---|---:|---:|---:|---:|---:|
|pixel shift|17.56%|22.68%|18.31%|17.48%|16.98%|
|exposure ramp|15.44%|19.95%|16.13%|15.35%|14.02%|

Values are mean **relative latent L1**, not RGB error, perceptual quality or runtime. Frozen inputs stay exact; cut/occlusion rows do not improve. Fitted coefficients around−0.106 favor pulling back rather than extrapolating forward on these two samples. One causal fit and one captured image do not establish generalization.

The practical next small experiment is two-anchor blending with hard invalidation of **both** histories on scene change. Current cut samples become static after the cut, so equal-input bypass can hide an old-scene contamination bug. Required new tests include cut-then-motion, motion reversal and moving local occlusion, followed by actual decoder RGB and temporal comparisons. Do not increase the reuse threshold merely because latent error fell. No HIP implementation, RGB validation or speedup is claimed for this pilot.

## Prioritized work

1. Add low-overhead gameplay reuse/recompute telemetry; current per-frame diagnostic logging synchronizes and must not be used as performance telemetry.
2. Start the contained two-anchor/reset/RGB test while preparing the single-FFN W4A4/rotation/low-rank feasibility study. These are separate experiments with separate costs and rollback.
3. If four-bit quality/cost clears the operator test, evaluate integration into a larger C256 FFN path. If local caching clears sensitivity checks, evaluate one C32 block/window.
4. Keep full Sol query-conditioned routing as an attention-quality/compute tradeoff candidate, not the first bet for a large whole-network gain.

All implementation candidates remain AttExp; R3 installed in Stellar Blade remains unchanged. Findings/plans are summarized in DevHistory.md; raw CPU results are summary.json and frames.csv here.
