param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# Multihead attention without the f32 score array: exp written from QK registers into the f16 Q/K slots.
$env:DLSS5_BUILD_FUSED_EXP='1'
& "$Folder\run_blocked_c32_ffn_network.ps1" -Folder $Folder
exit $LASTEXITCODE
