param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: ViT GEMMs with four token tiles per wave (weight traffic / 4); shaders compiled by run_vit_packed_input_network.ps1.
$env:DLSS5_VIT_BLOCK_M='4'
& "$Folder\run_vit_packed_input_network.ps1" -Folder $Folder
exit $LASTEXITCODE
