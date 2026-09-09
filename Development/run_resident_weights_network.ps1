param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# Copy every initialization-time weight/bias/map upload buffer to GPU-local memory.
$env:DLSS5_TEST_RESIDENT_WEIGHTS='1'
& "$Folder\run_coalesced_qkv_network.ps1" -Folder $Folder
exit $LASTEXITCODE
