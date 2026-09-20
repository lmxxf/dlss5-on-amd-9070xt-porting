# Image-check scheduling — rejected, 2026-09-20

Tested two host-only changes against R3:1) compute image tile statistics earlier on the same stream;2) run it on an auxiliary nonblocking stream while the encoder runs. Kernels, model, gain, thresholds and reuse decisions are unchanged. R3 preview remains selected; neither scheduling change was promoted or installed.

Schedule2 orders main input-ready→aux check→main join before the ViT decision. It retains system fences and disables only event timing. Cold/reset/dirty/history/mode transitions fall back to the original serial path. This experiment uses the existing R3 gfx1201 modules on RX9070XT; no new HSACO or game DLL was needed to measure it. Runner SHA256:19145f7905589ef3cc4ff3f8b0b7f00668fa8b9b0c5f642469e6d3435cf17230.

## Output checks

Frozen input, horizontal motion and1%-area dark patch,12 frames each, both schedules:72 reference/candidate pairs all bit-identical to R3. identity.csv preserves every SHA. This checks the tested ordering/data dependencies; it is not full multi-stream/gameplay certification.

## Formal timing

History-enabled horizontal-motion sequence,160-frame ABBA per case; discard first32, average the remaining128. No diagnostic state readback, RGB writing, compression or transfer during measurement. All below are complete replay wall times, not isolated shader times or game FPS.

| Schedule | Tier | R3 ms | Candidate ms | Added ms | Added time | Baseline drift ms |
|---|---:|---:|---:|---:|---:|---:|
|early same stream|900|12.696496|12.703922|0.007426|0.058%|0.005945|
|early same stream|1080|18.022957|18.047332|0.024375|0.135%|0.032820|
|aux stream|900|12.749285|13.175066|0.425781|3.340%|0.001570|
|aux stream|1080|18.055695|18.482715|0.427020|2.365%|0.006625|

Early same-stream scheduling offers no demonstrated improvement. Aux scheduling is a clear regression on this path. Diagnostic runs exaggerated that penalty further; their cold/dumping times are not used as production-performance evidence.

## Short span diagnostic and its limit

A separate40-frame900 history replay used the existing HIP span probe. After omitting10 startup samples, CPU enqueue means were0.519ms (schedule0),0.533ms (schedule1),0.535ms (schedule2). This does not support a large CPU-enqueue explanation for the0.43ms regression.

The GPU event timing channel returned negative elapsed values, and several implausibly near-zero spans, in schedules0/1 despite successful API status. Preserve these raw values in span-*.log/results.json; do not discard negative entries and average the rest into a performance claim. The parser explicitly marks those channels unusable. Consequently these data do not separate queue waits, synchronization, cache effects or GPU contention, and do not establish a driver root cause. Formal wall-time ABBA is the basis of rejection.

API contract references: [AMD HIP stream management](https://rocmdocs.amd.com/projects/HIP/en/latest/doxygen/html/group___stream.html), [event management](https://rocm.docs.amd.com/projects/HIP/en/latest/.doxygen/docBin/html/group___event.html). Additional streams permit concurrency; this experiment supplies the measured performance outcome for this small image check only, not a general verdict on HIP concurrency.

## Decision

Keep the existing R3 whole-game preview at D:\DLSSNR-Lab\AttExp-preview, DLL fd451efb…, unchanged. The experiment is isolated under Development/HIP/experiments/vit-image-overlap and disabled by default. Further dynamic work should target a larger compute component or a cheaper reuse predictor, rather than adding stream/event machinery to this small check.
