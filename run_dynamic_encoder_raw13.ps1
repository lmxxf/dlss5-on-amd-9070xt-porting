param([int]$Frame)
$ErrorActionPreference='Stop';$L='D:\DLSSNR-Lab';function P($b,$s=''){"$L\raw-$Frame-block$b$s.f32"};function Q($n){if($LASTEXITCODE){throw "$n failed"}}
& "$L\d3d12_swin32_chain.exe" (P 0 '-hwc') (P 4) `
  "$L\block1-effective.bin" "$L\block2-effective.bin" `
  "$L\block3-effective.bin" "$L\block4-body-effective.bin";Q 'resident blocks1-4'
& "$L\d3d12_downsample_enter_test.exe" (P 4) "$L\block4-downsample-matrix.bin" "$L\block5-enter-32x64.bin" (P 4 '-enter5') 1920 1088 32;Q down4
& "$L\d3d12_swin_chain.exe" (P 4 '-enter5') (P 8) `
  "$L\block5-body-effective.bin" "$L\block6-body-effective.bin" `
  "$L\block7-body-effective.bin" "$L\block8-body-effective.bin";Q 'resident blocks5-8'
& "$L\d3d12_downsample_enter_test.exe" (P 8) "$L\block8-downsample-matrix.bin" "$L\block9-enter-64x128.bin" (P 8 '-enter9') 960 544 64;Q down8
& "$L\d3d12_swin_chain.exe" (P 8 '-enter9') (P 13) `
  "$L\block9-body-effective.bin" "$L\block10-effective.bin" `
  "$L\block11-effective.bin" "$L\block12-effective.bin" `
  "$L\block13-effective.bin";Q 'resident blocks9-13'
