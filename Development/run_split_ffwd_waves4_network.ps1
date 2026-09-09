param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C512 ffwd with four waves per 16-token group (compiled by run_parallel_split_network.ps1 with NATIVE_SPLIT_FFWD_WAVES4=1).
$env:DLSS5_BUILD_SPLIT_FFWD_WAVES4='1'
& "$Folder\run_vit_split_k_network.ps1" -Folder $Folder
exit $LASTEXITCODE
