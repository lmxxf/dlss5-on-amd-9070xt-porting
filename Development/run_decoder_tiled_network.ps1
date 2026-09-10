param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: decoder entry (1024->512) f16 weights as contiguous 1KB tiles (-0.01ms; the small upsample projections got slower with tiles, so they stay). Bit-exact.
$env:DLSS5_BUILD_DECODER_TILED='1'
$env:DLSS5_DECODER_TILED='1'
& "$Folder\run_split_tiled_network.ps1" -Folder $Folder
exit $LASTEXITCODE
