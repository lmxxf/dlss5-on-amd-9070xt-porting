param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# Register-blocked split FFWD: one input staging per K step shared by four mix blocks.
$env:DLSS5_BUILD_SPLIT_FFWD_BLOCKED='1'
& "$Folder\run_wave_vit_qkv_network.ps1" -Folder $Folder
exit $LASTEXITCODE
