param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# Control: identical shaders and flags, synchronous per-submission waits, so
# per-stage GPU timestamps are not shifted by back-to-back execution.
$env:DLSS5_TEST_FORCE_SYNC='1'
& "$Folder\run_coalesced_qkv_network.ps1" -Folder $Folder
exit $LASTEXITCODE
