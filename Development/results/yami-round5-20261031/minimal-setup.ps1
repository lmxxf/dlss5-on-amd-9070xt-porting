$ErrorActionPreference='Stop'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\final-minimal\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\final-minimal\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\minimal-c32-wave1.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-minimal\modules\c32-wave1.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\minimal-c32-wave1-rtz.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-minimal\modules\c32-wave1-rtz.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\minimal-c32-wave1-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-minimal\modules\c32-wave1-fast.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\minimal-c32-wave1-fast-norm900.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-minimal\modules\c32-wave1-fast-norm900.hsaco"
