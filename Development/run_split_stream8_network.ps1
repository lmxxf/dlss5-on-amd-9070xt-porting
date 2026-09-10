param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C512 blocks without window pack/crop and QKV pack: kernels read the block input by raster index and write the cropped raster; the first
# projection's E4M3 output (tiles) is the QKV input; between blocks the raster is E4M3 bytes (first input / last output stay f32). Bit-exact.
# The eight stream kernels are compiled in scripts/bench.ps1 (they need every lower runner's build env); this runner only sets the flags.
$env:DLSS5_BUILD_SPLIT_STREAM8='1'
$env:DLSS5_SPLIT_STREAM8='1'
& "$Folder\run_split_ffwd8_network.ps1" -Folder $Folder
exit $LASTEXITCODE
