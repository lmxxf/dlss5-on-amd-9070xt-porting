param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
$env:DLSS5_TEST_MULTIHEAD_FOUR_WAVES='1'
& "$Folder\run_c32_four_wave_network.ps1" -Folder $Folder -EightWaves -ParallelExp -ParallelProb -ParallelNorm
exit $LASTEXITCODE
