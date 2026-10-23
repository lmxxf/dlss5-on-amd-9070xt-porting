# 1088 compact-tail eligibility: isolated candidate review

## Confirmed source dependency

Current `hip_reference_network.h:478,495` limits both `InitCompactContract` and `InitCompactProjection` to `W==1920 && H==1152`. At 1920×1088 the compact paired function pointers remain null, even though both shapes use 32×20 = 640 ViT tokens. The documented W16 masks are requested defaults, not proof that those two actual functions run.

That qualification disables more than storage compaction:

- Contract and projection cannot select their `compactw_w16` implementations.
- `Vit()` (line 886) requires `compact_contract_bstream_fn && compact_projection_bout_fn` for inter-block bytes.
- `VitEntryExitByteOk()` (line 1033) requires the same two handles for byte Gather and Up39 input.
- Thus 8 `vit_pack_input` launches can remain. Up39 byte-in W16 FP8 depends on `vit_entry_exit_byte`, so its October 17 path can remain off, while Up48's independent C512 byte input can still run.
- C512, C32 and ViT Expand W16 have separate qualification, so this does not imply all October 14 work was off.

The diagnostic will establish actual dispatch names/counts for cold and subsequent frames; source implications above are not a substituted runtime count.

## Why a two-condition experiment is justified

The candidate changes only the two `H==1152` terms to `(H==1152 || H==1088)`. It preserves device gfx1201/9070XT, FAST1, Original MP1, no graph/history/sparse, no custom module selector, and all function/layout pairing checks. It changes no kernel binaries or arithmetic source.

The core consumer layouts are token-indexed and independent of image height:

- Contract weights are 1024×4096 E4M3 with 1024 f32 scales; compact scale offset changes from 4194304 floats to 1048576 floats, with paired kernel dispatch (`CompactContractWeight`, lines 443–455).
- Projection weights are 1024×1024 E4M3 plus 1024 f32 scales; compact scale offset changes from 1048576 to 262144 floats (`CompactProjectionWeight`, lines 458–470).
- `hip/vit_stream.inc:270–281` passes `tokens` to the same contract/project bodies, with fixed channels and explicit compact scale offsets. Neither receives processing H.
- `hip/deep_fast.hip:763–795` contract and `hip/vit_stream.inc:21–65` projection index token rows and channels. W16 preserves the K16 accumulation order, the four-part reduction order and the output quantization used by their existing 1152 path.
- Upstream geometry/padding is unchanged: 1088 has a different real raster depth than 1152, but still pads the ViT grid to 640. We are not replacing upstream 1088 input with 1152 data.
- Byte edges represent values already quantized to the E4M3 grid. Enabling the paired path should preserve those values; this still requires actual full-frame output equality, particularly at the gather/Up39 boundary.

The original October 12 qualification was intentionally restricted to the profile tested then. This is an unexpanded validation scope, not evidence of a fundamental 1088 incompatibility. Before accepting the candidate require full-output finite/repeat and bit0 against current at 1088, then the same-batch performance gate. Do not infer acceptance from source proof alone.

## Diagnostic protocol

`trace.exe` and `candidate-trace.exe` share the ordinary positional interface but require warm=0 and frames=1..3. Recommended:

`TRACE_EXE ASSETS MODULES FLAGS PROCESSING_F32 OUTDIR 0 3 1088`

Each frame emits `frame-N.rgb32f`; repeated output is checked byte-for-byte. `GAP_DISPATCH` records every actual `hipModuleLaunchKernel` and `hipExtAnyOrderLaunch` call in the main Run path and SP init/run/recovery; `GAP_COUNT` counts launches, including recovery kernels even if they early-exit. The handle is mapped through the actual cached `functions` map, so it reports resolved kernel names including paired compact functions. Unknown handles appear as UNMAPPED and must be investigated. No observer/dump API is set; those would disable byte paths. No timings are collected. The trace wrapper buffers names until after synchronization; it affects host scheduling and is strictly a correctness/route diagnostic, never a performance arm.

`candidate.exe` uses the unchanged ordinary benchmark loop (80 warm, 160 measured, first/last raw outside timing) and is the performance arm. Its only source delta is `compact1088.patch`. Keep the existing `benchmark.exe` as actual-current-product control.
