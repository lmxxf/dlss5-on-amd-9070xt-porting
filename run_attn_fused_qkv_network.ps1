param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: multihead (C64/128/256) QKV+normalize fused into the attention dispatch (LDS Q/K/V).
$env:DLSS5_ATTN_FUSED_QKV='1'
& "$Folder\run_c32_attn_fast3_network.ps1" -Folder $Folder
exit $LASTEXITCODE
