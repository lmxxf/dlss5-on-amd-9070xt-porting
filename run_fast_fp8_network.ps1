param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH stage 2a: E4M3 operands in the Swin FFN (on top of stage 1 hardware accumulation).
$env:DLSS5_FP8_OPERANDS='1'
& "$Folder\run_fast_accumulate_network.ps1" -Folder $Folder
exit $LASTEXITCODE
