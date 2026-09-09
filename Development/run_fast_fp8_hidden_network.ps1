param([Parameter(Mandatory=$true)][string]$Folder,[switch]$Lds)
$ErrorActionPreference='Stop'
# FAST PATH stage 2b probe: ViT hidden layer as E4M3 (buffer-loaded operands); -Lds also packs LDS-staged operands.
$env:DLSS5_FP8_HIDDEN='1'
$env:DLSS5_FP8_LDS=[string][int]$Lds.IsPresent
& "$Folder\run_fast_fp8_network.ps1" -Folder $Folder
exit $LASTEXITCODE
