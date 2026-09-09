param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: batch level 2 -- ViT 4 layers per command list, decoder in two lists (~6 lists/frame). CPU-side only.
$env:DLSS5_BATCH_SUBMITS='2'
& "$Folder\run_c32_attn_fast4_network.ps1" -Folder $Folder
exit $LASTEXITCODE
