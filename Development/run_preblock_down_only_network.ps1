param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: preblock finish writes only the pooled buffer; post70 reads the preblock raw tiles as its merge skip (mode 6).
$env:DLSS5_PREBLOCK_DOWN_ONLY='1'
& "$Folder\run_vit_qkv_fused_network.ps1" -Folder $Folder
exit $LASTEXITCODE
