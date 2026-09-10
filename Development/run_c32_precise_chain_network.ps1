param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# Build-only reference: the C32 FFN activation polynomial, the merge-fold sum and the attention residual without FMA contraction
# (their contraction is context dependent on this driver). Same math as the default build (PSNR unchanged); the fused-FFN kernel is bit-exact against it.
$env:DLSS5_BUILD_C32_PRECISE_CHAIN='1'
& "$Folder\run_c32_epilogue_network.ps1" -Folder $Folder
exit $LASTEXITCODE
