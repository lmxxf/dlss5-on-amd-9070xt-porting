# Full71 native D3D12 experimental result

RX9070XT standalone, existing private Agility721/SM6.10 wave matrices. Actual full-model gold58885 exit0/released:1920x1152 processing, valid1920x1080,640 ViT tokens, same controlled inputb3d114..., style1/seed0/MP1/AEoff/historyoff. Both complete evaluations have SHA6cb6ecd... and validRGB bitdiff0; all6,220,800 valid components finite/nonzero, range[.0251312256,1], mean.512713909/variance.043703318. Actual Vit31 input640x1024 F32 (2,621,440B), SHA012df8bc..., was exported after execution and used by the separate native4-stage test.

Same1152 HIP-reference diagnostic: PSNR51.381953dB (peak1), MAE.00198341944/max.0329000354. No claim of current-HIP math/noise equivalence: the legacy HLSL's stage arithmetic and stored noise differ or are unproved. Created-PSO capture contains957 successful creations (including unused variants), not957 executed dispatches. The selected prefix raw-root fix is independently source-reviewed and actually passed; old failing selections and unsupported1088 remain in the failure ledger.

| Standalone implementation | Actual fresh pilot | GPU outer mean | Host mean |
|---|---|---:|---:|
| Legacy full71 native D3D12, synchronous chunked |42273,10warm/20measure|23.388310ms|23.574450ms|
| Existing validated full71 HIP at same1152/input |40214,10warm/20measure|9.171576ms|9.472550ms|

Every sample finite and positive; first/last repeats passed. These are sequential limited pilots, not ABBA/formal significance tests. Native D3D12 currently provides **no speedup**. Legacy outer GPU includes CPU/fence idle across chunk submissions; host additionally includes timestamp markers/map. HIP uses its own outer-event/stream completion segment and procedural-noise path. Do not interpret the14ms difference as API overhead, subtract from the older Mochi gap, or infer every D3D12 implementation is slower. No frame-render/Present FPS is measured.

The separate native4-stage test15399 is bit-exact/finite for four actual stages on this exported input. Its one-pair76655 timing improves GPU(.57816→.52192ms) while host worsens(.659→.788ms); not a stable positive or justification for whole-list execution. The standalone full HDR Frame integration29430 also passed finite/nonzero output; its first cold frame51.414ms is not performance acceptance. See the separate APP/fourstage receipts.

No Daniel binary ran, no game was launched, no player configuration/DLL/package was changed. No further GPU or micro-variant tests followed. A future fast backend needs an actual port of current fused HIP operators or separately validated resource/submit optimization; this prototype does not claim that work complete. Raw outputs remain under `/tmp/nr-dx12-pure-build/results` and remote isolated run directory; git records hashes/logs/statistics only.
