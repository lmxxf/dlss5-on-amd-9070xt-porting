param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# Blocked C32 FFN bound through root descriptors so its output uses the matrix Store on a raw UAV.
$env:DLSS5_TEST_C32_FFN_RAW_STORE='1'
& "$Folder\run_fused_shift_network.ps1" -Folder $Folder
exit $LASTEXITCODE
