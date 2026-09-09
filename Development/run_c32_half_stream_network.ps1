param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C32 ffn/raw scratch as f16 (every value on them is H()-rounded, so exact); halves the C32 blocks' memory traffic.
$env:DLSS5_BUILD_C32_HALF_STREAM='1'
$env:DLSS5_C32_HALF_STREAM='1'
& "$Folder\run_post70_low_main8_network.ps1" -Folder $Folder
exit $LASTEXITCODE
