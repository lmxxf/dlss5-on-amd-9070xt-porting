param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C512 blocks' QKV and projection weights as contiguous 512-byte tiles (kernels already supported it; host packing + build flag). Bit-exact.
$env:DLSS5_BUILD_SPLIT_TILED='1'
$env:DLSS5_SPLIT_TILED='1'
& "$Folder\run_vit_tiled_network.ps1" -Folder $Folder
exit $LASTEXITCODE
