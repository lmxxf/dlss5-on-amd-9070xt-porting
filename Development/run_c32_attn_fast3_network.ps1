param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C32 QKV+normalize fused into the attention dispatch (LDS Q/K/V, no aux round trip).
$env:DLSS5_BUILD_C32_ATTN_FAST3='1'
$env:DLSS5_C32_ATTN_FAST3='1'
& "$Folder\run_post70_merge_fold_network.ps1" -Folder $Folder
exit $LASTEXITCODE
