param([int]$Frame)
$ErrorActionPreference='Stop';$L='D:\DLSSNR-Lab';function P($b,$s=''){"$L\raw-$Frame-block$b$s.f32"};function Q($n){if($LASTEXITCODE){throw "$n failed"}};function B($b,$src,$w,$h,$shift,$weight="block$b-body-effective.bin"){& "$L\d3d12_block128_test.exe" "$L\$weight" $src (P $b) $w $h $shift;Q "block$b"}
$i = P 13;B 14 $i 480 272 1;& "$L\d3d12_downsample_enter_test.exe" (P 14) "$L\block14-downsample-matrix.bin" "$L\block15-enter-128x256.bin" (P 14 '-enter15') 480 272 128;Q down14
& "$L\d3d12_swin_chain.exe" (P 14 '-enter15') (P 21) `
  "$L\block15-body-effective.bin" "$L\block16-body-effective.bin" `
  "$L\block17-body-effective.bin" "$L\block18-body-effective.bin" `
  "$L\block19-body-effective.bin" "$L\block20-body-effective.bin" `
  "$L\block21-body-effective.bin";Q 'resident blocks15-21'
B 22 (P 21) 240 136 2;Move-Item -Force (P 22) (P 22 '-body');& "$L\d3d12_downsample_enter_test.exe" (P 22 '-body') "$L\block22-pool-identity.bin" "$L\block22-enter-256x512.bin" (P 22) 240 136 256;Q down22
$i = P 22
& "$L\d3d12_swin_chain.exe" $i (P 29) `
  "$L\block23-logical-effective.bin" "$L\block24-logical-effective.bin" `
  "$L\block25-logical-effective.bin" "$L\block26-logical-effective.bin" `
  "$L\block27-logical-effective.bin" "$L\block28-logical-effective.bin" `
  "$L\block29-logical-effective.bin";Q 'resident blocks23-29'
B 30 (P 29) 120 72 2 'block30-logical-effective.bin';Move-Item -Force (P 30) (P 30 '-body');& "$L\d3d12_downsample_enter_test.exe" (P 30 '-body') "$L\block30-pool-identity.bin" "$L\block30-enter-512x1024.bin" (P 30 '-34x60') 120 68 512;Q down30
