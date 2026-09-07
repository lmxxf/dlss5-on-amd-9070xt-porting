param([Parameter(Mandatory=$true)][string]$Folder,[switch]$Sync)
$ErrorActionPreference='Stop'
# Deferred submission: same command lists in the same queue order, but the CPU
# no longer waits on a fence after each of the ~512 per-frame submissions.
# -Sync reruns the identical binary in the old wait-per-submit mode as control.
if($Sync){$env:DLSS5_TEST_ASYNC_SUBMIT='0'}else{$env:DLSS5_TEST_ASYNC_SUBMIT='1'}
& "$Folder\run_vit_expand_chunk2_network.ps1" -Folder $Folder
exit $LASTEXITCODE
