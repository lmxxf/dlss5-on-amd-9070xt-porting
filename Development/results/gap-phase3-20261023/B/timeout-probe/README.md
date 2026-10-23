# B consumer bounded-timeout zero-output probe

CPU-built executable: `/home/lmxxf/work/dlss5-gap-phase3-20261023/B/timeout-probe.exe`. This builder has not executed GPU/RTC. Root must run under the existing GPU lock/game-check/disk/watchdog discipline.

Invocation:

`timeout-probe.exe MODULE_HSACO OUTPUT_DIRECTORY [HIP_DEVICE=0]`

Use the **trace-off** B `deep_fast-packed-fast.hsaco` containing `split_projection_frag_w16_bskip_chain`; no model assets are needed. ABI was confirmed with the B kernel author. Launch is 8 WG × 32 threads, tokens16, ready64 zero, target8, wait_limit1. No producer or downstream kernel is submitted, so all consumers must take the bounded timeout path.

All input/skip/weights/trace pointers refer to valid allocated readable poison buffers. FP32 output has 32768 payload bytes and byte-tiled output8192 bytes; each starts nonzero and has256-byte leading/trailing canaries. Ready64 and sticky status also have canaries. After an explicit private-stream drain, the program checks status exactly1, both output payloads bitwise all zero, all output/control canaries untouched, ready unchanged and all input/skip/weight/trace poison unchanged. Four readback dumps and `receipt.json` are written even on a numerical check failure; nonzero exit rejects failure. If stream completion becomes uncertain, it exits without freeing possibly live GPU buffers.

`collect.ps1` is a separate **CPU-only** reread of the raw dumps, independent zero/canary/status checking plus SHA of module/probe/receipts. It launches no GPU work. Args `-ModulePath ... -ProbePath ... -OutputDirectory ...`. Root can run it after successful guarded execution to bind the exact module identity to the receipt.

This test demonstrates the no-producer timeout behavior for this16-token shape. It does not prove mixed producer/consumer scheduling, absence of all deadlocks, or normal full-network overlap. The host duration is diagnostic, never a performance benchmark. Source/ABI/compiler hashes are in `build-receipt.json`. First build used one extra parent in the include path and failed before compilation; corrected build passed, original failure log retained.
