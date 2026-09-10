param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: the C32 FFN runs inside the fast4 attention dispatch (pre, blocks 1-4, 67-69, post); the ffn scratch becomes the second raw buffer. Exact.
$env:DLSS5_BUILD_C32_FUSED_FFN='1'
$env:DLSS5_C32_FUSED_FFN='1'
& "$Folder\run_c32_precise_chain_network.ps1" -Folder $Folder
exit $LASTEXITCODE
