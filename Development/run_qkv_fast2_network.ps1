param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: multihead fused QKV+normalize row sums of squares via f16 matrix stores + MMA against all-ones. Build-time only.
$env:DLSS5_BUILD_QKV_FAST2='1'
& "$Folder\run_attn_fast2_network.ps1" -Folder $Folder
exit $LASTEXITCODE
