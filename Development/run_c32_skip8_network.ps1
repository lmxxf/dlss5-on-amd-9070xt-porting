param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: block 4 finish writes E4M3 main8 (no f32 main, no crop); the block 66 upsample projection reads it as the skip residual. Exact.
# The skip8 cso is compiled in run_wave_decoder_network.ps1 (DLSS5_BUILD_C32_SKIP8) where the accumulate flags are already set.
$env:DLSS5_BUILD_C32_SKIP8='1'
$env:DLSS5_C32_SKIP8='1'
& "$Folder\run_preblock_main8_network.ps1" -Folder $Folder
exit $LASTEXITCODE
