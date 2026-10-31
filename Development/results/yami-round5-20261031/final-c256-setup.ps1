$ErrorActionPreference='Stop'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\final-c256\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-base\modules\*" "D:\DLSSNR-Lab\yami-round5-20261031\final-c256\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-full-c64-wave2.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-c256\modules\c64-wave2.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-full-c64-wave2-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-c256\modules\c64-wave2-fast.hsaco"
