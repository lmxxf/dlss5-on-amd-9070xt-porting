param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C32 FFN expand/contract weights tile-contiguous (host DLSS5_C32_TILED_WEIGHTS + kernel NATIVE_C32_TILED_WEIGHTS via DLSS5_BUILD_C32_TILED_WEIGHTS).
$env:DLSS5_BUILD_C32_TILED_WEIGHTS='1'
$env:DLSS5_C32_TILED_WEIGHTS='1'
& "$Folder\run_tiled_weights_network.ps1" -Folder $Folder
exit $LASTEXITCODE
