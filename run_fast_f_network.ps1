param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# Bit-level FP8 quantizer (legacy rounding semantics) in the blocked C32 FFN.
$env:DLSS5_BUILD_FAST_F='1'
& "$Folder\run_local_c32_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
