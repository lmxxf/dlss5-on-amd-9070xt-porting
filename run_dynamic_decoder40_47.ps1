param([int]$Frame = 1200)
$ErrorActionPreference = 'Stop'
$Lab = 'D:\DLSSNR-Lab'
$Input = Join-Path $Lab "ffx-color-$Frame-block39.f32"
$Shifts = @{40=0;41=1;42=3;43=2;44=0;45=1;46=3;47=2}
foreach($Block in 40..47){
 $Raw=Join-Path $Lab "ffx-color-$Frame-block$Block.f32"
 & (Join-Path $Lab 'd3d12_block128_test.exe') (Join-Path $Lab "block$Block-logical-effective.bin") $Input $Raw 120 72 $Shifts[$Block]
 if($LASTEXITCODE -ne 0){throw "decoder block$Block failed: $LASTEXITCODE"}
 if($Block -le 46){
  $Correction=if($Block -eq 40){'block39-to40-live-correction.bin'}else{"block$Block-live-correction.bin"}
  $Corrected=Join-Path $Lab "ffx-color-$Frame-block$Block-corrected.f32"
  & (Join-Path $Lab 'd3d12_affine_test.exe') (Join-Path $Lab $Correction) $Raw $Corrected 512 512
  if($LASTEXITCODE -ne 0){throw "decoder block$Block correction failed: $LASTEXITCODE"}
  $Input=$Corrected
 }else{$Input=$Raw}
}
