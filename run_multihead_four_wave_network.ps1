param([Parameter(Mandatory=$true)][string]$Folder,[switch]$ParallelSoftmax,[switch]$EightWaves)
$ErrorActionPreference='Stop'
$env:DLSS5_TEST_MULTIHEAD_FOUR_WAVES='1'
$env:DLSS5_TEST_MULTIHEAD_EIGHT_WAVES=[string][int]$EightWaves.IsPresent
$env:DLSS5_TEST_PARALLEL_MULTIHEAD_SOFTMAX=[string][int]$ParallelSoftmax.IsPresent
& "$Folder\run_c32_four_wave_network.ps1" -Folder $Folder -EightWaves -ParallelExp -ParallelProb -ParallelNorm
exit $LASTEXITCODE
