param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: pre-block prefix mix computed inside the FFN kernel (mapping mode 5): no prefix dispatch, no raw round trip.
$env:DLSS5_INLINE_PREFIX='1'
& "$Folder\run_c32_attn_fast3_network.ps1" -Folder $Folder
exit $LASTEXITCODE
