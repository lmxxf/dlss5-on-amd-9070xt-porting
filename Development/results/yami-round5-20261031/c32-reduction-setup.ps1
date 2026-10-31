$ErrorActionPreference='Stop'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\c32-dot2\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\c32-dot2\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\c32-dot2-c32-wave1-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\c32-dot2\modules\c32-wave1-fast.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\c32-dot2-c32-wave1-fast-norm900.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\c32-dot2\modules\c32-wave1-fast-norm900.hsaco"
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\c32-dotpar\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\c32-dotpar\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\c32-dotpar-c32-wave1-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\c32-dotpar\modules\c32-wave1-fast.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\c32-dotpar-c32-wave1-fast-norm900.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\c32-dotpar\modules\c32-wave1-fast-norm900.hsaco"
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\c32-wmma2\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\c32-wmma2\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\c32-wmma2-c32-wave1-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\c32-wmma2\modules\c32-wave1-fast.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\c32-wmma2-c32-wave1-fast-norm900.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\c32-wmma2\modules\c32-wave1-fast-norm900.hsaco"
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\c32-wmma4\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\c32-wmma4\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\c32-wmma4-c32-wave1-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\c32-wmma4\modules\c32-wave1-fast.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\c32-wmma4-c32-wave1-fast-norm900.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\c32-wmma4\modules\c32-wave1-fast-norm900.hsaco"
