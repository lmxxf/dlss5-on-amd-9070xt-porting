param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C32 stages read their input through a mapping in the FFN kernel (no pack pass; crop only where consumed).
if(-not $env:DLSS5_BUILD_C32_MAPPED_INPUT){$env:DLSS5_BUILD_C32_MAPPED_INPUT='1'}
if(-not $env:DLSS5_C32_MAPPED_INPUT){$env:DLSS5_C32_MAPPED_INPUT='1'}
& "$Folder\run_vit_block_m_network.ps1" -Folder $Folder
exit $LASTEXITCODE
