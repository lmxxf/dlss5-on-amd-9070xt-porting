param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: decoder entry / upsample projection epilogue: LDS-staged tile, float4 coalesced writes, bit-level F (no log2/exp2). Bit-exact.
# (DLSS5_BUILD_DECODER_HW_H / DLSS5_BUILD_DECODER_RESIDUAL_GRID are the two follow-up switches tested separately.)
$env:DLSS5_BUILD_DECODER_FAST='1'
& "$Folder\run_c32_merge4_network.ps1" -Folder $Folder
exit $LASTEXITCODE
