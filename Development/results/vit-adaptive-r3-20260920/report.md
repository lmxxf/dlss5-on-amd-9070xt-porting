# R3: exact-input reuse extension and fused anchor commit

AttExp,2026-09-20. Compare directly with frozen R2 (ae58f32) as well as exact-stream baseline. No approximation thresholds changed: feature0.22/local1.0/coherent image0.35, max approximate period4.

The equality test compares every FP32 bit of the gathered ViT input, including padding and signed zero. If it is identical to the last exact anchor, mode1 may pass the age deadline; other guards remain. Age saturates at period, and a subsequent changed input after expiry must refresh. Mode2/mode3/period1 retain their forced schedules. Diagnostic reason8 identifies exact extension. Matching input here means the entire block31–38 function has matching inputs/weights; the current-frame encoder skips and decoder still execute normally.

Image-anchor commit now shares the finish kernel, eliminating one launch. R3 is a matched host/module ABI; R2 runner/modules have been frozen separately.

Validation:
-900/1080 disabled, forced-full, forced schedule and adaptive static controls all preserve exact-stream hashes.
-16 direct GPU checks cover exact extension, padding-only changes, signed-zero bits, forced-full, period1, age saturation, nonfinite values and fused image copying on400/640 tokens.
-Seven900 sequences:168 R2/R3 SHA pairs (reference and adaptive frames) all identical. Thus R2 per-frame quality measurements remain applicable to these same captures; R3 did not add output differences on them. This is not a new independent gameplay dataset.

Timing uses160-frame ABBA, discards first32, reports means, no diagnostic capture/transfer/compression while running. Static/no-history and horizontal motion/history are measured separately. Tiny movement-time differences are reported rather than assumed to be speedups.

R3 DLL fd451efb4aa566795f17ca6d9e1b2166f4c99f68e9e5fd6e63b75a0590cef696; deep gfx1200 c909d8c146e6822e861ca78176f2c2965b9165c4db71b9cbd2eadba442769c51; gfx1201 09b03f12cfb521291067f646cadd30c1630b98e729c644d25dfec2013574fba8.

## Timing results

| Reference | Case | Tier | Reference ms | R3 ms | Saved ms | Time reduction |
|---|---|---:|---:|---:|---:|---:|
|R2 adaptive|static|900|12.395457|12.056695|0.338762|2.733%|
|R2 adaptive|static|1080|17.223500|16.660605|0.562895|3.268%|
|R2 adaptive|motion + history|900|12.739082|12.743168|-0.004086|-0.032%|
|R2 adaptive|motion + history|1080|18.015629|18.036285|-0.020656|-0.115%|
|R2 adaptive|motion + history repeat|900|12.681031|12.681078|-0.000047|-0.000%|
|R2 adaptive|motion + history repeat|1080|17.987918|17.987223|0.000695|0.004%|
|exact-stream|static|900|13.295891|12.094609|1.201281|9.035%|
|exact-stream|static|1080|18.782582|16.669379|2.113203|11.251%|
|exact-stream|motion + history|900|13.429574|12.768098|0.661477|4.926%|
|exact-stream|motion + history|1080|19.270594|18.062254|1.208340|6.270%|

Static R3 saves an additional0.339/0.563ms versus R2. Movement is effectively unchanged at this measurement resolution: the first1080 comparison regressed0.0207ms (0.115%), but repeat difference reversed to−0.0007ms;900 repeat differs by0.00005ms. Do not claim an extra movement speedup or universal non-regression from this. Overall against exact-stream: static9.04%/11.25%, motion/history4.93%/6.27%. These are controlled replay times, not actual-game FPS.

## Final preview

R3 promoted only into lab staging `D:\DLSSNR-Lab\AttExp-preview`. Current50-file manifest attached; gfx1201 payload matches all24 tested modules. R2 runner/modules, build and DLL retained under frozen/r2 names. Mode off/on, history reset/force-full and isolated Install/Install/Restore all passed again after promotion. No real game directory modified. Physical F8/overlay delivery, actual motion vectors and subjective quality are the next whole-game test.
