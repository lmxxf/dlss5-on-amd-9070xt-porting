param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: float bilinear temporal coordinate/sample passes (no fixed-point/double, no reciprocal table).
$env:DLSS5_FAST_TEMPORAL='1'
& "$Folder\run_fast_fp8_qkv_network.ps1" -Folder $Folder
exit $LASTEXITCODE
