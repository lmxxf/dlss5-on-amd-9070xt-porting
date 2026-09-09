param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: multihead (C64/128/256/512) attention fast2 - register exp, MMA row sums, hardware E4M3 P and output tile. Build-time only.
$env:DLSS5_BUILD_ATTN_FAST2='1'
& "$Folder\run_wave_c32_ds_network.ps1" -Folder $Folder
exit $LASTEXITCODE
