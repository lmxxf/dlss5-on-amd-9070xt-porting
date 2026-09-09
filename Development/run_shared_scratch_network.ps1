param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# Memory diet: multihead block scratch and C32 ffn/raw shared across serially executed blocks.
$env:DLSS5_TEST_SHARED_SCRATCH='1'
$env:DLSS5_TEST_SHARED_C32_SCRATCH='1'
& "$Folder\run_c32_ffn_raw_store_network.ps1" -Folder $Folder
exit $LASTEXITCODE
