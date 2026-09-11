param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FIX: the hardware E4M3 cast (dx::linalg Cast<F8_E4M3FN>) does not saturate: a value beyond +-448 becomes NaN. In the fused C32 block the
# residual stream (FFN input / attention input), the FFN hidden layer and the AV output are cast without the exact chain's F() saturation,
# so a bright flat region (Magpie's 8-bit sRGB input pushes the tail residual past 448) turns one token NaN, the 8x8 attention window
# follows, and the rgb head's clamp() writes 0 -- the black staircase blocks. Clamp to +-448 before those casts. Bit-identical on the
# reference fixture; replay of the Magpie dump frames 2002/1503: 25920/44352 NaN head values -> 0.
$env:DLSS5_BUILD_C32_SAT_CAST='1'
& "$Folder\run_decoder_out16_network.ps1" -Folder $Folder
exit $LASTEXITCODE
