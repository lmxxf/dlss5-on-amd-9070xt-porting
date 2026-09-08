param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# Blocked C32 FFN writes its output with the matrix Store through a raw UAV.
$env:DLSS5_TEST_C32_FFN_RAW_STORE='1'
& "$Folder\run_split_c32_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
