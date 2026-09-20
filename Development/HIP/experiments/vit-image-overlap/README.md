# Early/overlapped image-statistics experiment

R3 kernel math and thresholds unchanged. Isolated host generated under /tmp/vit-image-overlap-src; uses existing R3 modules. `DLSS5_VIT_IMAGE_SCHEDULE=0` original scheduling,1 early on the main stream,2 early on a nonblocking auxiliary stream. Default0; no preview deployment by these scripts.

Warm mode1/adaptive frames only. At RunGraph entry, main already waits for the D3D12 input fence and contains the previous anchor update. For schedule2, record ready on main, wait ready on aux, launch the image check there and record done. Main joins done before any ViT decision, reset or mode-off return. Thus encoder work may overlap the independent image check; actual concurrency/speed is an empirical question. Retain system fences; only event timing is disabled. Cold state/dirty input/history/seed/gap/reset/profile/graph paths fall back to the original check. Destruction drains both queues before destroying events or buffers.

Sources for API contracts: [AMD HIP stream management](https://rocmdocs.amd.com/projects/HIP/en/latest/doxygen/html/group___stream.html), [event management](https://rocm.docs.amd.com/projects/HIP/en/latest/.doxygen/docBin/html/group___event.html). StreamNonBlocking=1 and EventDisableTiming=2. Optional exports loaded only for schedule2; no production hip_api.h edits.

build-host.sh builds the isolated replay runner. run.ps1 Smoke compares frozen/pan/local-occlusion frames bitwise with R3; Timing compares history-enabled pan900/1080 with R3 via160-frame ABBA, excluding first32. Do not collect or transfer during timing. Neither an extra queue nor theoretical overlap is accepted as a speedup without these measurements.
