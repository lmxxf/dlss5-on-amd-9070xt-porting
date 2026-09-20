# Sparse C32 contraction128→32 — 2026-09-20

User requested contraction after expansion-only failed to improve runtime. Restored original expansion, pruned contraction2:4 by magnitude, used native FP8 SWMMAC inside the fused C32 kernel. Offline host packing, no extra GPU launch. Both gfx1200/gfx1201 compiled; RX9070XT executed all tests with game/Magpie absent.

Two layouts tested: (1) shared-memory transpose reuses scratch.hidden/raw with identical132-byte row pitch and wave-owned16 rows; after all hidden reads, writes contracted output then synchronizes and restores original accumulator coordinates; (2) wave-register transpose exchanges low lane/vector bits with3 butterfly stages, then swaps high lane bits, avoiding the shared-memory output/synchronization. Both add precomputed residual after contraction instead of using it as the initial MMA accumulator; floating-point addition order therefore changes.

Whole-network900P frozen capture, no history/ViT reuse, host wall timing,160 frames/first32 excluded, ABBA:

| Variant | Original | Candidate | Delta |
|---|---:|---:|---:|
| Shared-memory transpose |13.24958ms|13.26950ms|+0.01992ms|
| Wave-register transpose |13.24786ms|13.56221ms|+0.31436ms|

Shared-memory path has no demonstrated gain; register version is slower. Retained all8 per-frame timing CSVs and summary.json. No GPU event or microbenchmark-to-game extrapolation.

Both variants' final RGB SHA4103d899d3b02dd7edfee8c3b5b2b1c85f22491b4a161bc4d680b3a5837f5013 matches exactly. Original75aaba5ef94368353f3f17c034b53918151aa2ebc98ff870b2a057d08ae910ab. Original dense FP8 with identically pruned contraction weights gives dbd68fcfad47192c5f258f9cdbf610899b59957175e6d11a3d9901268d8be6d2. Sparse vs dense-pruned MAE0.000000025/255, max0.02821/255, p99zero: consistent with tiny floating-point order effects on this sample, not bit identity. Versus unpruned original MAE1.08546/255,p9918.55225/255,max71.46973/255. All captured outputs finite. This is one frozen capture, not temporal/gameplay quality validation or an exhaustive operator oracle.

Decision: neither adopted; no DLL/game deployment. Both C32 expansion and contraction experiments currently fail speed criterion despite big ViT FFN's positive result. This does not establish a hardware-wide inability to accelerate small channels, only these fused implementations. Keep C64 separate. Further C32 work should examine actual stage costs before adding more sparse rearrangement machinery.
