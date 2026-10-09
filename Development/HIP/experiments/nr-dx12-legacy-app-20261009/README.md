# Legacy full71 D3D12 APP baseline — CPU preparation

`prepare.py` copies current headers and builds the existing `benchmark_vit_reuse.cpp` with `DLSS5_COMPARE_HLSL=1` and official O2/static MinGW flags. Current Frame has one unconditional HIP `DirectInput` reference; the private copy gates that block and rejects active output temporal mode on legacy HLSL. Production source is untouched. Final CPU build28809 passed; original92881 compile failure is recorded in the preparation receipt.

Use `legacy-full71.flags`, based on the last historical native fast43 profile. It preserves WaveMatrix/FP8 selection and safe `BATCH_SUBMITS=2`, removing the historical skipped blocks, output smoothing, debug dumps and residency experiments. MP1/AE0/history0/Style1/seed0 are fixed. APP requests1080 valid/1152 processing; actual Network70 geometry/token argument is logged. Do not equate this APP geometry with the separate1088 pureNN experiment.

Current9070 prerequisites were read, not changed: DXC/dxcompiler1.10.2605.24 at `D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64`, header `inc\hlsl\dx\linalg.h`, AgilityCore1.721.3.0, driver32.0.31007.2048, developer mode1. No AGS or driver installation is needed for this prototype.

Existing `D:\DLSSNR-Lab\native-game-tiled-assets` contains207CSO and a65-source manifest.57 NN shader sources match currentgit;8 codec/temporal/UI sources differ. `stage.ps1 -Stage` stages private assets and overlays only currently used codec/input shader sources. It does not run GPU work. VitAttentionFP8 was rebuilt with the existing exact recipe and DXC and matches the legacy CSO byte-for-byte: SHA256 `4a28227a27cb994d1cec5bbf59dbac59491740fd4e4c300b8b3b955b387b95bf`; dump has Wave32 and real LinAlg FP8/F16/F32 matrices. This proves that one shader binding, not all207CSO.

Correctness-first command arguments: `ASSETS FLAGS INPUT_RGBA16F OUTPUT_PREFIX 1 0`; the input is the old1296×720 frozen HDR menu. Stage `D3D12\D3D12Core.dll` from the existing matrix-probe beside the executable. Single-list `RecordUnsubmitted` retains its historical device-hang guard and is not used.

This is an old mathematical implementation, not a same-math backend swap: HLSL half RNE/softwareFP8/softmax division/accumulation order differ from current HIPFAST1. Initial finite outputs and error must be measured before timing; any speed difference is a legacy-prototype comparison until math is aligned. No GPU experiment or player installation has been performed by this preparation task.

After main pureCreate exposed the wrong prefix root selection, the private header now also includes `prefix_wave` in the raw-store root condition (the source fix validated in math’s3110 build). CPU rebuild21603 passed; this prevents repeating that known prefix PSO failure. No1088 tail guard is bypassed; APP remains1152.
