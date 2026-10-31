# One held-out validation, fixed before running

Candidate: qualified paired ViT RTZ, direct prewarmed-handle binding (no per-frame suffix allocation). Constructor probes only1920x1152; eligibility uses the current frame's explicit AE0 mode, not the stale constructor snapshot.

Reason: the initial short binder trial had all three mean deltas negative but a third-round p99 counterexample and baseline drift up to63us. No production promotion yet. This validation will not replace or erase any earlier result.

One predeclared three-round ABBA, Small1080 only,800 frames/slot,discard160,640 steadyframes/slot,3840 samples/arm. Existing15s watchdog retained; no retry to select a favorable outcome. Require exact first/lastGold, finite, actualpair route; report pooledmean/p99 and allthree round directions and control drift. If the held-out result does not support improvement without tail regression, do not promote this variant. All historical trials stay in the report.
