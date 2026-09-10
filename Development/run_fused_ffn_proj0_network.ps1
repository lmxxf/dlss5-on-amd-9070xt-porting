param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: multihead Swin blocks' FFN (expand+contract) and FFN output projection in one dispatch (26 blocks; kernels compiled in run_fp8_activations). Exact against the precise chain.
$env:DLSS5_BUILD_FUSED_FFN_PROJ0='1'
$env:DLSS5_FUSED_FFN_PROJ0='1'
& "$Folder\run_c32_fused_ffn_network.ps1" -Folder $Folder
exit $LASTEXITCODE
