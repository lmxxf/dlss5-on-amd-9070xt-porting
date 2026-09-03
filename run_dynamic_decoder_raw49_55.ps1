param([int]$Frame)
$ErrorActionPreference='Stop';$L='D:\DLSSNR-Lab'
& "$L\d3d12_swin_chain.exe" `
  "$L\raw-$Frame-block48.f32" "$L\raw-$Frame-block55.f32" `
  "$L\block49-body-effective.bin" "$L\block50-body-effective.bin" `
  "$L\block51-body-effective.bin" "$L\block52-body-effective.bin" `
  "$L\block53-body-effective.bin" "$L\block54-body-effective.bin" `
  "$L\block55-body-effective.bin"
if($LASTEXITCODE){throw 'resident blocks49-55 failed'}
