# INT4 grouped expansion tile sweep — 2026-09-20

Tested N128 for increased input reuse and N16 for reduced accumulator pressure against the existing N64. Quantization, K64 scaling, activation and output precision stay identical. Both architectures compile; gfx1201 ran on RX9070XT. Games/Magpie were absent before and after each run.

| Sweep | FP8 control | INT4 N64 control | Candidate |
|---|---:|---:|---:|
| N16 |35.519µs|69.530µs|72.980µs|
| N128 |35.463µs|68.597µs|86.799µs|

These are isolated complete input-quantization + expansion pairs, synchronized host wall timing. Each sweep alternates candidate order3,5,5,3, with FP8 ABBA around each candidate. Full raw timings in CSV; summary.json preserves means and byte comparisons. Neither candidate wins. N64 itself is substantially slower than the earlier ~37µs sample while FP8 remains ~35µs; the submission/resource sensitivity remains unresolved. Do not compare the new candidate against an earlier session's best control or attribute the change to a proven driver/register cause.

All four artifacts (packed input, scales, preactivation float32 and output FP8) are byte-identical between each new tile and N64. Timed batches also checked output bytes against their initial result. This validates unchanged arithmetic, not end-to-end RGB quality of INT4 versus FP8.

Final combined module SHA256: gfx1200 d8bc84071e1f6607f3816a9f371e1bc6bd0c855947749b70c72ba9ca4c46bf1a; gfx1201 ea5e8fc8e78307f5422b9adcc595674264bdd3ec435972a33b2dc53295b1900a. N128-only build used for first sweep: gfx1200 b345a02f86b182c06a789a6f88908203777c1f238e785a249628ecdfe7c6ff6a; gfx1201 fd12165b9ef0c429e3149d382de128d75a72a5b42dca00f0879ca65c313b1e1d.

Decision: reject both tiles for adoption. Keep experimental exports and reproducible runner only; game stays R3. Next useful test remains full-network insertion with discarded output against an extra original FP8 expansion, followed separately by RGB quality if timing warrants adoption. No further blind tile sweep planned.
