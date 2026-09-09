param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: multihead projections with E4M3 output skip the per-element F(H()) pass; the Cast<F8> store is the quantizer.
# Build-time only (same cso names); run_fp8_stream_network.ps1 reads the variable when it compiles the f8out variants.
$env:DLSS5_BUILD_DIRECT_CAST='1'
& "$Folder\run_split_direct_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
