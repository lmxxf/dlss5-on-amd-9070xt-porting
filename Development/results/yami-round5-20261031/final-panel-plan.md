# Final payload qualification — fixed before execution

A stock b60; B fixed ViT perm + C512 probability clamp removal; C B + exact guarded four-accumulator dot2 ONLY in the existing norm900 module; D C + C256 one-plane/RC-refresh modules. Main C32 module is unchanged. Canonical guarded sources are compiled; gfx1201 code/metadata match the independently tested candidates; gfx1200 executable code is unchanged.

Seven profiles, three rotated symmetric eight-slot rounds, 800 frames/slot discard160,3840samples/arm/profile,168slots. Actual input fixtures and full asset trees are SHA-pinned before the run. No other remote operations during timing. All candidates first pass seven staticGold references. Earlier batches and failures remain intact.

Qualification contract (engineering thresholds, not proof of no regression):
- Every accepted candidate: all7 pooledmean regression<=0.5%, pooledp99 regression<=1%, no profile with all3 roundmeans regression>0.5%; sum of7 mean deltas<0; Small1080 all3 mean and p99 improve.
- C/D additionally: Original960 all3 mean and p99 improve versus A.
- Prefer C over B only if C versus B wins Original960 mean all3 rounds and pooledp99, with all7 noninferiority bounds intact.
- Prefer D over C only if D versus C wins mean all3 rounds on BOTH Original1088 and Small1080, with pooledp99 not worse on both and all7 bounds intact.
- Evaluate each candidate against its own observations. If none qualifies, retain b60. No repeat of this identical payload to erase unfavorable data.

These target-specific gates reflect the measured specialization: norm900 targets Original960 among standard profiles. They do not require untargeted profiles to show fictitious speedups. Report every positive delta, and never label a noninferiority result “7/7 strict wins”. Any chosen payload still needs full configuration/ABI validation before integration.
