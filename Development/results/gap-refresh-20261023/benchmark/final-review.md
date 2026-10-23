# Independent CPU review of final measurement evidence

Reviewed the raw per-slot CSVs, Mochi aggregate stdout, command receipts, trace stdout/stderr, raw-hash receipts, and the isolated trace patch. Recomputed numbers independently without calling the production analysis scripts; results are in `final-review.json`. No new experiment was run.

## Numerical claims supported

| Comparison | Independently recomputed A | B | A−B |
|---|---:|---:|---:|
| Current versus locked old Mochi | 8.533050354 ms | 7.867127083 ms | **0.665923271 ms** |
| October 6 HIP versus current, same batch | 8.923111219 ms | 8.554250771 ms | **0.368860448 ms** |
| Current versus compact1088 candidate | 8.570213292 ms | 8.354502188 ms | **0.215711104 ms** |

All three comparisons contain three four-slot rounds, 160 measured frames per slot, 960 frames per arm. The round-level directions agree with the summaries. Candidate pooled p99 is lower by **0.245399100 ms**; all three round p99 differences are negative. The historical-optimization claim is supported by today's O/N comparison, not by subtracting absolute October 6 times from today's times.

Mochi only emits aggregate GPU totals for each slot, rounded to 0.001 ms for 160 frames. Its per-frame p99 does not exist here. Do not manufacture a paired Mochi p99, or imply nanosecond measurement precision from nine decimal digits in derived means.

## Trace and correctness claims supported

Current and candidate each have three complete traces; frame 0, 1 and 2 sequences are identical within each executable. Every index is contiguous, no handle is UNMAPPED.

- Current: **161 launches/frame**, 8 `vit_pack_input`, compact contract/projection and both W16 flags 0, ViT entry/exit byte qualification 0.
- Candidate: **153 launches/frame**, 0 `vit_pack_input`, compact contract/projection and both W16 flags 1, ViT entry/exit byte qualification 1.
- Both include **6 SP launches/frame** (two init/run/recovery triples), so recovery dispatches were not silently dropped from the count.
- Actual any-order launch count is **0** in both traces. This is not a tracing omission: the isolated patch wraps both normal `hipModuleLaunchKernel` and the `ext_launch(...hipExtAnyOrderLaunch)` branch, plus explicit SP calls. Production destructor independently reports `lifetime_pdl_calls=0`. `pdl_effective=1` is capability, not proof that an any-order launch occurred in this profile.

The six trace frames, ordinary candidate Gold first/last, and 24 timed-run first/last receipts all report the same full 25,067,520-byte output SHA256: `153ac018f5dd5744cff9157661c46c469d01db6018a97c93aec2b1e2e05647f1`. Normal benchmark stdout checks finite values and repeated output. The main O/N output hashes also agree. Local evidence contains the hash receipts rather than the large raw payloads, so this final CPU review independently checks receipt consistency, not a second download/hash of every raw file.

## Reporting limits

1. **0.215711 ms is the combined effect of widening the two qualification gates**, including compact weight storage, W16 contract/projection, ViT byte edges and enabled downstream Up39 path. It is not an isolated cost for eight dispatches or 120 MiB alone.
2. The candidate was not timed interleaved with Mochi in the same NCCN run. Subtracting its 8.3545 ms from the earlier Mochi 7.8671 ms is only a cross-run indication, not a refreshed candidate-versus-Mochi controlled gap. Likewise do not mechanically deduct 0.215711 from 0.665923 and label the result measured.
3. Numeric equality is established for the controlled gradient/input and frozen Original/FAST1/1088 profile. This does not establish all dynamic inputs, other shapes, GPU targets, or game compatibility, and does not itself merge the candidate into production.
4. The trace changes host-side work and is diagnostic only; all performance values above come from the non-trace executables.

Conclusion: the three requested headline numbers, current **161→153** route change, compact **0→1**, and unchanged controlled output are supported by the available evidence. No blocking inconsistency was found.
