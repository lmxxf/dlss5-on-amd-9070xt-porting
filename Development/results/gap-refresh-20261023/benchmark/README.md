# Current Original 1x pure-NN benchmark — CPU preparation

Built from repository HEAD `c24e3a97cdf596d0c997cee7deda91c981d520ce`; exact header dependency hashes are in `source-lock.json`. No GPU or remote RTC run was performed by this preparation task.

- Executable: `/home/lmxxf/work/dlss5-gap-refresh-20261023/benchmark.exe`
- SHA256: `27561d9baa571897ba005a82c65b93e10e708732de8ff0a4960046ac40cd7f44`
- Rebuild: `bash Development/results/gap-refresh-20261023/benchmark/build.sh` from any working directory.
- Modules: `/home/lmxxf/work/dlss5-five-build/final-payload/HIP/gfx1201`; all 80 dual-architecture payload files match both current `hip/SHA256SUMS` and the final October 17 manifest. The repository tracks the checksum manifest, not the HSACO binaries. `module-audit.json` records every comparison.
- All 11 required new decoder/Down/W16/byte-stream exports checked in the actual ELF symbol tables are present (`exports.json`). This is availability evidence; active-path evidence must also come from the ensuing run.

## Same input/timing contract

The positional interface is unchanged:

`benchmark.exe ASSETS MODULES FLAGS PROCESSING_F32 OUTDIR 80 160 1088`

Use the original October 6 assets/fixture/`fast1-1088.flags` and a private copied current module directory. The original run used `D:\DLSSNR-Lab\history-trial-041a-20261006\assets`, `D:\DLSSNR-Lab\sync-network-gap1080-20261006\fast1-1088.flags`, and its `fixture\processing1088.rgba32f`. The controlled input SHA is `3ef42d34cebd1bc847e5caa3a95b3c3b70f45c9eeed4e828a3e2f85615b49b68`; verify the remote files before running.

The original warm/measurement loop is unchanged: initial full-NN run + first raw readback, 80 warm frames, then 160 frames measured by outer HIP begin/end events and synchronized per frame; last raw readback after timing. Output repetition and finiteness are checked. CPU wall time is separate. No per-kernel events were added.

`production_options.generated.h` was freshly extracted from current `NativeHipNetwork::Create`, using current `NativeApplyHipEnvironment` and header defaults. It is not the old generated factory. Explicit Original 1x and disabled Small admission prevent today's default 0.5x from contaminating the result. Width 1920, processing height 1088, post shift 3, full71/MP1/Style1/seed0/AE0/historyoff match the previous comparison. PDL1 is retained through the same flags; SubmitPulse has no bridge activation in this pure network host.

Current header defaults select C512 W16=2, C512 FFN W16=1, C32 W16=1, ViT W16=7, inter-byte=15, decoder F8 W16=3 and grouped Down F8 W16=on. The benchmark prints compile-time defaults before execution and the production destructor logs actual W16 call counts and byte-stream counts. These masks do not on their own prove every candidate dispatch executed. The parent runner must keep an uncontaminated environment (clear inherited NR_/DLSS5_ before applying the locked flags), record module identities, and guard GPU access.
