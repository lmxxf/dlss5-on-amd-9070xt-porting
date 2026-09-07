param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
$env:DLSS5_TEST_SUBMISSION_TIMING='1'
& "$Folder\run_wave_vit_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
