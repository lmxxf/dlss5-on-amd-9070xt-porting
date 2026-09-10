param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# NULL RESULT (not in the chain; bench.ps1 keeps the flags at 0): ViT expand (1024->4096) + contract (4096->1024) in one dispatch per block (native_wave_vit_ffn_fused.hlsl): one group per 16 tokens,
# 16 waves; hidden quarter q is produced into LDS as E4M3 and consumed as contract K partition q. Same MMA order, same
# activation/rounding, same partition-sum order as expand m4 + split-K reduce + combine, so bit-exact; what disappears is the
# E4M3 hidden raster (2.6MB) and the four f32 partial rasters (10.5MB) written and read per block, plus two dispatch boundaries.
# Measured ~2x slower per block in both group sizes (latency-bound: too few waves per SIMD, dependent MMA chains); kept as a record.
$env:DLSS5_BUILD_VIT_FUSED_FFN='1'
$env:DLSS5_BUILD_VIT_FUSED_TOK32='1'
$env:DLSS5_VIT_FUSED_FFN='1'
$env:DLSS5_VIT_FUSED_TOK32='1'
& "$Folder\run_decoder_out16_network.ps1" -Folder $Folder
exit $LASTEXITCODE
