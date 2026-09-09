param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C32 attention one group = two windows (all 8 waves do QKV, per-wave softmax, 2 group syncs).
$env:DLSS5_BUILD_C32_ATTN_FAST4='1'
$env:DLSS5_C32_ATTN_FAST4='1'
& "$Folder\run_batch_submits_network.ps1" -Folder $Folder
exit $LASTEXITCODE
