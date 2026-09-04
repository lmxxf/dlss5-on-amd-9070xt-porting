param([int]$Frame)
$ErrorActionPreference='Stop';$L='D:\DLSSNR-Lab'
& "$L\d3d12_vit_chain_amd.exe" `
  "$L\raw-$Frame-block30-body.f32" "$L\raw-$Frame-block38.f32"
if($LASTEXITCODE){throw 'resident blocks31-38 failed'}
