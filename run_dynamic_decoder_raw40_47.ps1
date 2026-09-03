param([int]$Frame)
$ErrorActionPreference='Stop';$L='D:\DLSSNR-Lab'
& "$L\d3d12_swin_chain.exe" `
  "$L\raw-$Frame-block39.f32" "$L\raw-$Frame-block47.f32" `
  "$L\block40-logical-effective.bin" "$L\block41-logical-effective.bin" `
  "$L\block42-logical-effective.bin" "$L\block43-logical-effective.bin" `
  "$L\block44-logical-effective.bin" "$L\block45-logical-effective.bin" `
  "$L\block46-logical-effective.bin" "$L\block47-logical-effective.bin"
if($LASTEXITCODE){throw 'resident blocks40-47 failed'}
