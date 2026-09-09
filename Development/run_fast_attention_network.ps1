param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH stage 3b: attention scalar segments (normalize, exp, denominator, probability, residual) without intermediate roundings.
$env:DLSS5_FAST_ATTENTION='1'
& "$Folder\run_fast_epilogue_network.ps1" -Folder $Folder
exit $LASTEXITCODE
