param([int]$Frame = 1200)
$ErrorActionPreference='Stop';$L='D:\DLSSNR-Lab';function P([int]$b,[string]$s=''){Join-Path $L "ffx-color-$Frame-block$b$s.f32"};function Check([string]$n){if($LASTEXITCODE-ne 0){throw "$n failed: $LASTEXITCODE"}};function B32($b,$Source,$shift){$weight=if($b-le 3){"block$b-effective.bin"}else{"block$b-body-effective.bin"};& "$L\d3d12_block1_test.exe" "$L\$weight" $Source (P $b) 1920 1088 $shift;Check "block$b"};function B($b,$Source,$w,$h,$shift){$weight=if($b-ge 10-and$b-le 13){"block$b-effective.bin"}else{"block$b-body-effective.bin"};& "$L\d3d12_block128_test.exe" "$L\$weight" $Source (P $b) $w $h $shift;Check "block$b"};function C($b,$c,$file="block$b-live-correction.bin"){$o=P $b '-corrected';& "$L\d3d12_affine_test.exe" "$L\$file" (P $b) $o $c $c | ForEach-Object {Write-Host $_};Check "block$b correction";return $o}
$i = P 0 '-hwc'; B32 1 $i 0
$i = P 1; B32 2 $i 1
$i = P 2; B32 3 $i 1
$i = P 3; B32 4 $i 1
& "$L\d3d12_downsample_enter_test.exe" (P 4) "$L\block4-downsample-matrix.bin" "$L\block5-enter-32x64.bin" (P 4 '-enter5') 1920 1088 32; Check 'block4 downsample'
B 5 (P 4 '-enter5') 960 544 0; $i = C 5 64
B 6 $i 960 544 1; $i = C 6 64
B 7 $i 960 544 3; $i = C 7 64
B 8 $i 960 544 2
& "$L\d3d12_downsample_enter_test.exe" (P 8) "$L\block8-downsample-matrix.bin" "$L\block9-enter-64x128.bin" (P 8 '-enter9') 960 544 64; Check 'block8 downsample'
B 9 (P 8 '-enter9') 480 272 0; $i = C 9 128
$sh=@{10=1;11=3;12=2;13=0}; foreach($b in 10..13){B $b $i 480 272 $sh[$b]; $i = C $b 128}
B 14 $i 480 272 1
& "$L\d3d12_downsample_enter_test.exe" (P 14) "$L\block14-downsample-matrix.bin" "$L\block15-enter-128x256.bin" (P 14 '-enter15') 480 272 128; Check 'block14 downsample'
B 15 (P 14 '-enter15') 240 136 0; $i = C 15 256
$sh=@{16=1;17=3;18=2;19=0;20=1;21=3}; foreach($b in 16..21){B $b $i 240 136 $sh[$b]; $i = C $b 256}
B 22 $i 240 136 2
Move-Item -Force (P 22) (P 22 '-body')
& "$L\d3d12_downsample_enter_test.exe" (P 22 '-body') "$L\block22-pool-identity.bin" "$L\block22-enter-256x512.bin" (P 22) 240 136 256; Check 'block22 downsample'
