param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: block 69 finish writes E4M3 main8 (no f32 main, no crop); post70's merge fold reads the bytes as the low-res input (mode 9). Exact.
$env:DLSS5_POST70_LOW_RAW='2'
& "$Folder\run_c32_skip8_network.ps1" -Folder $Folder
exit $LASTEXITCODE
