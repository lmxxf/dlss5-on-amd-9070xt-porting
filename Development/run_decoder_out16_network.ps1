param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: the upsample projections 56/62/66 write their raster as f16 (every value is H()/F(), on the f16 grid); the first block after each
# reads it as f16 instead of f32 (C32 fused FFN map mode 10; multihead f16-source mapped pack + f16-feature mapped projection). Rasters 17.7/35/70MB f32 halved, each written once and read once. Bit-exact.
$env:DLSS5_BUILD_DECODER_OUT16='1'
$env:DLSS5_DECODER_OUT16='1'
& "$Folder\run_c32_bias_tile_network.ps1" -Folder $Folder
exit $LASTEXITCODE
