param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# Static 8-element accumulator loops in the blocked C32 FFN (probe for dynamic matrix element indexing cost).
$env:DLSS5_BUILD_STATIC_LENGTH='1'
& "$Folder\run_local_c32_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
