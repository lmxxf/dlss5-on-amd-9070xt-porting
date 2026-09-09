param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: multihead projection + QKV weights tile-contiguous (host DLSS5_TILED_WEIGHTS + kernel NATIVE_TILED_WEIGHTS via DLSS5_BUILD_TILED_PQ).
$env:DLSS5_BUILD_TILED_PQ='1'
$env:DLSS5_TILED_WEIGHTS='1'
& "$Folder\run_ffn_tiled_weights_network.ps1" -Folder $Folder
exit $LASTEXITCODE
