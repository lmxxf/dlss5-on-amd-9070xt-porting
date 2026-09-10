param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C512 FFWD output stored as E4M3 bytes (it is already F(H())-quantized) and the following projection loads its A tiles directly. Build-only, bit-exact.
$env:DLSS5_BUILD_SPLIT_FFWD8='1'
& "$Folder\run_split_ffwd_tiled_network.ps1" -Folder $Folder
exit $LASTEXITCODE
