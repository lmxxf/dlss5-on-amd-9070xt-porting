param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: post70 without pack/crop/finish (FFN mapped from the merged raster, rgb head indexes the raw tiles). Runtime-compiled native_post70.hlsl.
$env:DLSS5_POST70_DIRECT='1'
& "$Folder\run_c32_attn_fast2_network.ps1" -Folder $Folder
exit $LASTEXITCODE
