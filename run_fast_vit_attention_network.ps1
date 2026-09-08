param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: ViT attention without software H()/legacy F(), FP32 accumulation across keys (compiled by run_vit_attention_half_network.ps1).
$env:DLSS5_BUILD_FAST_VIT_ATTENTION='1'
& "$Folder\run_c32_fp8_qkv_network.ps1" -Folder $Folder
exit $LASTEXITCODE
