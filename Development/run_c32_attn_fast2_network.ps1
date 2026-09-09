param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: C32 attention fast2 (register exp, MMA row sums, hardware E4M3 P and attention output, FP8 x FP8 projection).
# Host DLSS5_C32_ATTN_FAST2 packs the E4M3 projection copy; kernel built with NATIVE_C32_ATTN_FAST2 via DLSS5_BUILD_C32_ATTN_FAST2.
$env:DLSS5_BUILD_C32_ATTN_FAST2='1'
$env:DLSS5_C32_ATTN_FAST2='1'
& "$Folder\run_c32_ffn_fast3_network.ps1" -Folder $Folder
exit $LASTEXITCODE
