param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: fused FFN weights repacked so every B tile is one contiguous 512-byte block (host DLSS5_FFN_TILED_WEIGHTS + kernel NATIVE_TILED_WEIGHTS must agree).
$env:DLSS5_BUILD_TILED_WEIGHTS='1'
$env:DLSS5_FFN_TILED_WEIGHTS='1'
& "$Folder\run_matrix_residual_network.ps1" -Folder $Folder
exit $LASTEXITCODE
