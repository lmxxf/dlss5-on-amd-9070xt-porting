param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: fewer command lists per frame (ViT per layer, decoder per 3 stages). CPU-side only.
if(-not $env:DLSS5_BATCH_SUBMITS){$env:DLSS5_BATCH_SUBMITS='1'}
& "$Folder\run_inline_prefix_network.ps1" -Folder $Folder
exit $LASTEXITCODE
