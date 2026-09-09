param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C32 attention with one wave per window and no group synchronisation (compiled by run_c32_fused_attention_network.ps1).
$env:DLSS5_BUILD_C32_WAVE_ATTENTION='1'
& "$Folder\run_split_direct_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
