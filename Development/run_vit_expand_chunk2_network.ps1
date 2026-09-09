param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
$env:DLSS5_TEST_VIT_EXPAND_CHUNK2='1'
& "$Folder\run_multihead_four_wave_network.ps1" -Folder $Folder -ParallelSoftmax -EightWaves
exit $LASTEXITCODE
