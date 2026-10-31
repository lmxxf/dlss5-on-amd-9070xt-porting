$ErrorActionPreference='Stop'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\combined\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\combined\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\c512-prob-compile\c512-m32-mh.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\combined\modules\c512-m32-mh.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\vit-fix-compile\deep_fast-packed-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\combined\modules\deep_fast-packed-fast.hsaco"
