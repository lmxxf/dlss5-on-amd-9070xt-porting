param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: wave-matrix E4M3 QKV projection for the multihead blocks.
$env:DLSS5_FP8_QKV='1'
& "$Folder\run_fast_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
