param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: post70 rgb head one thread per pixel, features read once, f32 dot products with a single f16 rounding.
$env:DLSS5_POST70_FAST_RGB='1'
& "$Folder\run_preblock_main8_network.ps1" -Folder $Folder
exit $LASTEXITCODE
