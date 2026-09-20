# C32 sparse FFN experiments (AttExp)

Current experiment replaces only128→32 contraction inside the existing C32 fused FFN/attention kernel. Expansion remains original. Magnitude2:4 pruning retains the largest absolute effectiveFP8 values per K4 group. No extra GPU launch. Sparse weights/indices are appended after packed residual diagonals at load time.

Prepare R3 first with ../vit-adaptive/build-host.sh. prepare.py copies /tmp/vit-adaptive-src to /tmp/c32-sparse-src, patches host weight preparation and the fused kernel. Default uses wave-register butterfly transpose; --lds selects shared-memory transpose. contract.inc and contract-lds.inc retain both. The expansion-only historical experiment is in commit5c908c8 (expand.inc retained for reference).

Both contraction variants accumulate the pruned contraction from zero, then add the existing residual. This changes FP32 addition order versus the original residual-in-accumulator path. Do not demand bit identity to the original dense-pruned pipeline; measure that difference separately. The two transpose variants do match one another exactly in the tested full-network output.

Host environment DLSS5_C32_SPARSE=0 or1 leaves original dense weights and prepares the sparse side data;2 zeros matching dense contraction weights for the dense-pruned control. Sparse kernel is compile-specialized and always reads sparse side data. Disabling requires selecting original modules, not merely changing this environment value. These are lab controls, not a ready game switch.

Build benchmark_c32_sparse.exe with the command from ../vit-adaptive/build-host.sh and /tmp/c32-sparse-src as source root. Upload runner to D:/DLSSNR-Lab/hip-backend, generated kernel and build.ps1/run.ps1 to c32-sparse subfolder. build.ps1 builds gfx1200/gfx1201 and creates c32-sparse-modules from frozen R3 modules plus candidate. run.ps1 Check uses original modules for original/dense-pruned controls; Timing uses160-frame whole-network900P ABBA, first32 excluded, reuse/history disabled, game/Magpie process guards. Current results folder is c32-contract-shuffle-results; archive it before repetitions. LDS run archived separately.

Results: LDS13.2496→13.2695ms (no gain); shuffle13.2479→13.5622ms (regression). Both sparse RGB outputs identical; compared with dense-pruned original, only rounding-scale differences on the frozen sample. Neither candidate is adopted or deployed. See results/c32-contract-sparse-20260920. C64 untouched.
