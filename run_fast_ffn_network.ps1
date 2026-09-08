param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH stage 1a: Swin FFN with hardware FP32 accumulation over the full K (inexact by design).
$env:DLSS5_FAST_ACCUMULATE='1'
$env:DLSS5_TEST_ALLOW_INEXACT='1'
& "$Folder\run_shared_scratch_network.ps1" -Folder $Folder
exit $LASTEXITCODE
