param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: post70 reads block 69's raw tiles through the merge fold (mode 8, Ffast); block 69 skips finish+crop. Exact.
$env:DLSS5_POST70_LOW_RAW='1'
& "$Folder\run_preblock_main8_network.ps1" -Folder $Folder
exit $LASTEXITCODE
