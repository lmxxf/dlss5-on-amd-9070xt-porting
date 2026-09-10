param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: ViT expand/reduce weights and the hidden layer as 512-byte contiguous tiles (no power-of-two row strides). Pure data movement, bit-exact.
$env:DLSS5_BUILD_VIT_TILED='1'
$env:DLSS5_VIT_TILED='1'
& "$Folder\run_c32_fused_ffn_network.ps1" -Folder $Folder
exit $LASTEXITCODE
