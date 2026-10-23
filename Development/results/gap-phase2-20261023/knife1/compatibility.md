# Knife 1 CPU artifacts and compatibility protocol

Final production change is limited to the two compact Contract/Projection geometry gates in `hip_reference_network.h`, adding Original-only 1920×1088 and 1600×960 to existing 1920×1152. Original 1152 behavior, including previously allowed Small, is preserved. Snapshot and executable identities are in `build-receipt.json`; production diff in `production.patch`. No kernel binary or weight asset is changed. This builder did not execute GPU/RTC.

## Artifacts

All binaries are in `/home/lmxxf/work/dlss5-gap-phase2-20261023/knife1/`.

- `pure-baseline.exe` / `pure-candidate.exe`: old pure-NN timing boundary, Original1x forced, explicit no-Small admission. Args `ASSETS MODULES FLAGS PROCESSING_F32 OUTDIR WARM MEASURE PH`. PH=960 means 1600×960/400 tokens, PH=1088/1152 means 1920 wide/640 tokens. Gold uses 1 warm/1 measure; timing 80/160.
- `frame-baseline.exe` / `frame-candidate.exe`: current unmodified `benchmark_live_capture.cpp`, same official O2 build recipe. Args `ASSETS FLAGS INPUT1296x720_RGBA16F PREFIX frames temporal MODULES reset_every edges_only seed pattern`. Flags explicitly select MP1, network tier and rows; 3 frames with edges_only=0 for per-frame correctness; 240 frames with edges_only=1 and discard80 for timing. Pattern0=real input; patterns1/2=synthetic HDR alternatives.
- `trace-baseline.exe` / `trace-candidate.exe`: diagnostic-only complete dispatch receipts and fallback testing. Args `ASSETS MODULES FLAGS PROCESSING_F32 OUTDIR 0 3 PH SCENARIO`. Output `frame-0/1/2.rgb32f`. No performance claims may use traces.

## Diagnostic scenarios

| Scenario | Expected route |
|---|---|
| default | 1152 compact on in both arms; 1088/960 old off, candidate on |
| fast0 | DLSS5_FAST_NUMERIC=0, compact off both arms |
| mp2 | cold Original2x, compact off both arms |
| hot-mp | Original1→2→1, compare first and third outputs; existing compact handles may remain selected at hot MP2 |
| missing-contract | inject hipModuleGetFunction failure for all compact contract variants; projection may remain compact, interbyte disabled |
| missing-projection | inject failure for compact projection variants; contract may remain compact, interbyte disabled |
| missing-bstream | inject failure for compact contract bstream variants only; compact non-bstream still valid, interbyte disabled |
| missing-bout | inject failure for compact projection byte-output variants only; compact F32 projection still valid, interbyte disabled |

Missing-symbol scenarios are explicit API fault injection in the isolated `hip_api.h`, not a claim that a physically damaged module was constructed. The real module loader still runs; all other GetFunction requests forward to the original function pointer. Injected requests log `GAP_GETFUNCTION_INJECT symbol=... result=500 module_handle=...`. The ordinary production header and normal timing executables contain no injector.

For default/fallback cases compare every candidate frame hash with the baseline at the same case/geometry. FAST0 must compare to FAST0, MP2 to MP2. For hot-mp the harness compares repeated mode1 frames internally and the parent must additionally compare the MP2 frame across arms. A cold-MP2 test alone does not cover hot-MP2. Small assets are unnecessary and this harness deliberately blocks Small admission.

The trace wraps all normal and any-order launches plus explicit SP init/run/recovery. No observer/dump callback is installed, so byte-edge eligibility is not invalidated by the observer. All available and missing handle selections can be checked in `GAP_PROFILE`, and actual kernels in `GAP_DISPATCH`. These diagnostic wrappers do extra host work and are never performance arms.

## 900 source scope

Original900 uses 25×16 = 400 tokens, divisible by16. Contract/project kernels index runtime token rows and fixed 1024-channel matrices; compact scale offsets and weight shape do not depend on token count. Existing 900 Gather and Up39 shapes remain unchanged. Nonetheless W16 projection differs from the legacy non-W16 path in skip-add placement, so only actual multi-input complete-output checks can validate equality at 400 tokens; the 640-token historical Gold is insufficient.

Small900 is normally 1600×1024/448 and remains outside the new geometry gate. An artificially constructed Small processing1600×960 would use a different token grid; the final additional `!small_active` check excludes it (the intermediate timed candidate did not yet have this scope restriction). The existing 1152 Small behavior is preserved. Source reviewer also identified an artificial pre-seeded legacy weight-cache plus later-enabled compact combination outside normal public initialization; this harness does not claim that combination covered.

## Final production scope

`final-pure.exe`, `final-frame.exe`, `final-trace.exe` supersede their candidate counterparts for final acceptance. Baseline/intermediate candidate files are retained unchanged to preserve valid timing evidence. Final SHA identities are in `final-build-receipt.json`; final diff is `final-production.patch`, and the narrowing-only delta is `candidate-to-final.patch`.

`small_active` is initialized by `SelectModel(dlss5_mode::Requested())` early in the Network constructor, before module loading and the calls to both InitCompact methods. It is the actual selected model, not only requested mode or caller geometry. For the tested Original three profiles the new `!small_active` term is true, so final and intermediate candidate select identical paths. The final predicate leaves the original 1920×1152 case unchanged and closes the untested Small variants of the newly added dimensions. Final Gold still must be run against the final executable identities; existing intermediate-candidate timing is not relabeled as a new final-binary performance run.
