param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
$env:DLSS5_TEST_PRELOAD_C64_CONTRACT='1'
& "$Folder\run_multihead_av_network.ps1" -Folder $Folder
exit $LASTEXITCODE
