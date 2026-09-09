param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: multihead projection residual (feature*scale) as three E4M3 MMAs instead of a per-element decode/scale/H pass.
# Build-time only for the f8in non-mapfeature variants (same cso names); host packs the diagonal matrices unconditionally.
$env:DLSS5_BUILD_MATRIX_RESIDUAL='1'
& "$Folder\run_direct_cast_network.ps1" -Folder $Folder
exit $LASTEXITCODE
