param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
$env:DLSS5_TEST_COALESCED_FINISH='1'
& "$Folder\run_wave_downsample_network.ps1" -Folder $Folder
exit $LASTEXITCODE
