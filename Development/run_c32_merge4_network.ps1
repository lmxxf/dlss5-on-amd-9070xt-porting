param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: post70 merge fold in the fused C32 FFN prologue reads 4 channels per 4-byte load (4 passes instead of 16). Build-only, bit-exact.
$env:DLSS5_BUILD_C32_MERGE4='1'
& "$Folder\run_split_stream8_network.ps1" -Folder $Folder
exit $LASTEXITCODE
