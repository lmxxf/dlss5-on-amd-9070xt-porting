param([int]$Frame)
$ErrorActionPreference='Stop';$L='D:\DLSSNR-Lab';function P($b,$s=''){"$L\raw-$Frame-block$b$s.f32"};function Q($n){if($LASTEXITCODE){throw "$n failed"}}
& "$L\d3d12_swin32_chain.exe" (P 0 '-hwc') (P 4) `
  "$L\block1-effective.bin" "$L\block2-effective.bin" `
  "$L\block3-effective.bin" "$L\block4-body-effective.bin";Q 'resident blocks1-4'
& "$L\d3d12_swin_chain.exe" (P 4) (P 8) `
  "$L\block5-body-effective.bin" "$L\block6-body-effective.bin" `
  "$L\block7-body-effective.bin" "$L\block8-body-effective.bin";Q 'resident blocks5-8'
& "$L\d3d12_swin_chain.exe" (P 8) (P 14) `
  "$L\block9-body-effective.bin" "$L\block10-effective.bin" `
  "$L\block11-effective.bin" "$L\block12-effective.bin" `
  "$L\block13-effective.bin" "$L\block14-body-effective.bin";Q 'resident blocks9-14'
