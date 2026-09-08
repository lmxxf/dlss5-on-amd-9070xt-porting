param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C32 FFN hidden layer via hardware E4M3 cast, FP8 contract weights (compiled by run_blocked_c32_ffn_network.ps1 with NATIVE_C32_FFN_FP8=1).
$env:DLSS5_BUILD_C32_FFN_FP8='1'
& "$Folder\run_c32_ffn_fast2_network.ps1" -Folder $Folder
exit $LASTEXITCODE
