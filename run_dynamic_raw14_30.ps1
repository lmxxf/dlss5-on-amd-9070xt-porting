param([int]$Frame)
$ErrorActionPreference='Stop';$L='D:\DLSSNR-Lab';function P($b,$s=''){"$L\raw-$Frame-block$b$s.f32"};function Q($n){if($LASTEXITCODE){throw "$n failed"}};function B($b,$src,$w,$h,$shift,$weight="block$b-body-effective.bin"){& "$L\d3d12_block128_test.exe" "$L\$weight" $src (P $b) $w $h $shift;Q "block$b"}
& "$L\d3d12_swin_chain.exe" (P 14) (P 22 '-body') `
  "$L\block15-body-effective.bin" "$L\block16-body-effective.bin" `
  "$L\block17-body-effective.bin" "$L\block18-body-effective.bin" `
  "$L\block19-body-effective.bin" "$L\block20-body-effective.bin" `
  "$L\block21-body-effective.bin" "$L\block22-body-effective.bin";Q 'resident blocks15-22'
& "$L\d3d12_swin_chain.exe" (P 22 '-body') (P 30 '-body') `
  "$L\block23-logical-effective.bin" "$L\block24-logical-effective.bin" `
  "$L\block25-logical-effective.bin" "$L\block26-logical-effective.bin" `
  "$L\block27-logical-effective.bin" "$L\block28-logical-effective.bin" `
  "$L\block29-logical-effective.bin" "$L\block30-logical-effective.bin";Q 'resident blocks23-30'
