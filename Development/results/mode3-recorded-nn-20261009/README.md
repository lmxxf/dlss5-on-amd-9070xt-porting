# Actual AMD MODE3 on a recorded complete-NN output

Run6631 exited0 and released the atomic GPU lock. The uploaded originals were the October6 already-encoded controlled gradient (SHA3ef42d34...) and its coupled full71 FAST1/MP1/style1/seed0 NN result (SHA153ac018...). MODE3 processed that recorded output once, in place; NN was not rerun. This is neither current gameplay nor decoded HDR. Game/RTC checks,100GB disk guard,15s game watchdog and60s own-process bound were used. Source/shader/game configurations were not changed.

Actual statistics, RGB only over1920×1080, excluding the8 processing padding rows and all alpha:

| Metric | Actual |
|---|---:|
| Changed pixels |99.9964313%|
| Mean absolute before/after change |0.01797921583|
| Maximum absolute change |0.07511830330|
| Mean original NN residual |0.03646828979|

Effective RGB remained finite. Gate proportions are CPU estimates, not hardware counters: low-support failure/out-of-domain/zero residual0; headroom-zero0.00356867%. The actual after SHA is11b10952cccbb2033f43e3b90adbf653b6c7b30bd9464ac344e6e17f8ae894f5. The full26MB raw remains `/tmp/mode3-real-output-20261009/gpu/after.rgb32f`; it is not checked into git.

The initial reader used NumPy nearest-half, which differed from actual AMD output by MAE5.817285e−6. Modeling the half observation/guide packing toward zero reduces disagreement to MAE1.944902e−10/max5.960464e−8. This is an empirically matched model for this provider/data, not a universal rounding assertion or bit-exact CPU gold. Original nearest-half analysis is retained as`analysis-rne-original.json`; production MODE3 math was not changed.

`comparison.png` contains the actual recorded NN output, actual filtered output and encoded absolute difference×10. It was inspected with view_image and explicitly labels controlled encoded gradient/non-game/non-HDR. It proves the actual shader can add a nonzero correction at full image extent. It does not establish current-game perceptual improvement. The representative real-HDR-menu Frame test is a separate same-NN/same-decoder diagnostic prepared by the other agent; its evidence must not be mixed with this gradient case. No performance or deployment claim.
