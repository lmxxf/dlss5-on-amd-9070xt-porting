$ErrorActionPreference='Stop'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\c256-one-plane\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\c256-one-plane\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\c256-one-plane-c64-wave2.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\c256-one-plane\modules\c64-wave2.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\c256-one-plane-c64-wave2-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\c256-one-plane\modules\c64-wave2-fast.hsaco"
