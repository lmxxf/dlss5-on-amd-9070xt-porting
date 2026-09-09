param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C32 Q/K/V as E4M3 in aux via hardware cast; attention loads FP8 (compiled by run_c32_fused_attention_network.ps1 with NATIVE_C32_FP8_QKV=1).
$env:DLSS5_BUILD_C32_FP8_QKV='1'
& "$Folder\run_hw_h_network.ps1" -Folder $Folder
exit $LASTEXITCODE
