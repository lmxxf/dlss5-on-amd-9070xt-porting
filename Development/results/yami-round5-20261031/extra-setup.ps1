$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\vit-fix\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\vit-fix\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\vit-fix-compile\deep_fast-packed-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\vit-fix\modules\deep_fast-packed-fast.hsaco"
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\c512-den\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\c512-den\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\c512-compile\candidate\c512-m32-mh.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\c512-den\modules\c512-m32-mh.hsaco"