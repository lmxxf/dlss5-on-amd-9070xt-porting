$ErrorActionPreference='Stop'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\final-base\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\final-base\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-base-deep_fast-packed-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-base\modules\deep_fast-packed-fast.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-base-c512-m32-mh.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-base\modules\c512-m32-mh.hsaco"
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\final-norm\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\final-norm\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-norm-deep_fast-packed-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-norm\modules\deep_fast-packed-fast.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-norm-c512-m32-mh.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-norm\modules\c512-m32-mh.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-norm-c32-wave1-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-norm\modules\c32-wave1-fast.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-norm-c32-wave1-fast-norm900.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-norm\modules\c32-wave1-fast-norm900.hsaco"
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\final-full\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\final-full\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-full-deep_fast-packed-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-full\modules\deep_fast-packed-fast.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-full-c512-m32-mh.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-full\modules\c512-m32-mh.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-full-c32-wave1-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-full\modules\c32-wave1-fast.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-full-c32-wave1-fast-norm900.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-full\modules\c32-wave1-fast-norm900.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-full-c64-wave2.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-full\modules\c64-wave2.hsaco"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\final-full-c64-wave2-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\final-full\modules\c64-wave2-fast.hsaco"
