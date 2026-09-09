param([int]$Frame)
$ErrorActionPreference='Stop';$L='D:\DLSSNR-Lab'
& "$L\d3d12_swin_chain.exe" `
  "$L\raw-$Frame-block56.f32" "$L\raw-$Frame-block61.f32" `
  "$L\block57-body-effective.bin" "$L\block58-body-effective.bin" `
  "$L\block59-body-effective.bin" "$L\block60-body-effective.bin" `
  "$L\block61-body-effective.bin"
if($LASTEXITCODE){throw 'resident blocks57-61 failed'}
