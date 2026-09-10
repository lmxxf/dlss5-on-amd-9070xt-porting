param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C512 FFWD f16 weights (mix/expand/contract) as contiguous 1KB tiles (no 1024/256/512-byte row strides). Bit-exact.
$env:DLSS5_BUILD_SPLIT_FFWD_TILED='1'
$env:DLSS5_SPLIT_FFWD_TILED='1'
& "$Folder\run_decoder_tiled_network.ps1" -Folder $Folder
exit $LASTEXITCODE
