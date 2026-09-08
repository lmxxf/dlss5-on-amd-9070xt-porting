param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C32 FFN with matrix-block LDS/output stores and the raw input tile in LDS for the residual
# (compiled by run_blocked_c32_ffn_network.ps1 with NATIVE_C32_FFN_FAST2=1).
$env:DLSS5_BUILD_C32_FFN_FAST2='1'
& "$Folder\run_c32_fused_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
