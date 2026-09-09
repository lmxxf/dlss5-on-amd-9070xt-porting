param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: preblock finish writes Main as E4M3 bytes; post70 merge fold reads them (mode 7). Exact (Main was F()-quantized).
$env:DLSS5_PREBLOCK_MAIN8='1'
& "$Folder\run_vit_qkv_fused_network.ps1" -Folder $Folder
exit $LASTEXITCODE
