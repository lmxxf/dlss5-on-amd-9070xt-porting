param([int]$Frame = 1200)
$ErrorActionPreference = 'Stop'
$Lab = 'D:\DLSSNR-Lab'
$Input = Join-Path $Lab "ffx-color-$Frame-block22.f32"
$Shifts = @{23 = 0; 24 = 1; 25 = 3; 26 = 2; 27 = 0; 28 = 1; 29 = 3}
foreach ($Block in 23..29) {
    $Raw = Join-Path $Lab "ffx-color-$Frame-block$Block.f32"
    $Corrected = Join-Path $Lab "ffx-color-$Frame-block$Block-corrected.f32"
    & (Join-Path $Lab 'd3d12_block128_test.exe') `
        (Join-Path $Lab "block$Block-logical-effective.bin") `
        $Input $Raw 120 72 $Shifts[$Block]
    if ($LASTEXITCODE -ne 0) { throw "block$Block failed: $LASTEXITCODE" }
    & (Join-Path $Lab 'd3d12_affine_test.exe') `
        (Join-Path $Lab "block$Block-live-correction.bin") `
        $Raw $Corrected 512 512
    if ($LASTEXITCODE -ne 0) { throw "block$Block correction failed: $LASTEXITCODE" }
    $Input = $Corrected
}
