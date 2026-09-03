param([int]$Frame = 1200)
$ErrorActionPreference = 'Stop'
$Lab = 'D:\DLSSNR-Lab'
$Input = Join-Path $Lab "ffx-color-$Frame-block30-corrected.f32"
foreach ($Block in 31..38) {
    $QkvMain = if ($Block -eq 31) { 'block31-qkv-effective.fp8' } else { "block$Block-qkv-main.fp8" }
    $QkvWork = if ($Block -eq 31) { 'block31-qkv-work-effective.f16' } else { "block$Block-qkv-work.f16" }
    $Raw = Join-Path $Lab "ffx-color-$Frame-block$Block.f32"
    $Corrected = Join-Path $Lab "ffx-color-$Frame-block$Block-corrected.f32"
    & (Join-Path $Lab 'd3d12_vit_block31_test.exe') `
        (Join-Path $Lab "block$Block-vit-expand-effective.f16") `
        (Join-Path $Lab 'block31-vit-contract.f16') `
        (Join-Path $Lab 'block31-vit-contract-skip.f16') `
        (Join-Path $Lab $QkvMain) `
        (Join-Path $Lab $QkvWork) `
        (Join-Path $Lab 'block31-vit-projection.f16') `
        (Join-Path $Lab 'block31-vit-projection-skip.f16') `
        $Input $Input $Raw
    if ($LASTEXITCODE -ne 0) { throw "ViT block$Block failed: $LASTEXITCODE" }
    & (Join-Path $Lab 'd3d12_affine_test.exe') `
        (Join-Path $Lab "vit-block$Block-live-correction.bin") `
        $Raw $Corrected 1024 1024
    if ($LASTEXITCODE -ne 0) { throw "ViT block$Block correction failed: $LASTEXITCODE" }
    $Input = $Corrected
}
