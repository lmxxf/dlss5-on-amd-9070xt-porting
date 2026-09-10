param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# NULL RESULT (kept off): C32 attention softmax bias as one accumulator-layout load per tile — the driver emits the same 32 per-lane scattered loads (identical ISA, spill unchanged), so no gain.
$env:DLSS5_BUILD_C32_BIAS_TILE='0'
& "$Folder\run_decoder_fast_network.ps1" -Folder $Folder
exit $LASTEXITCODE
