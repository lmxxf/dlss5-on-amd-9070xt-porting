$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\token-pair\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\token-pair\modules"
Copy-Item "D:\DLSSNR-Lab\yami-round5-20261031\token-pair-compile\deep_fast-packed-fast.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\token-pair\modules\deep_fast-packed-fast.hsaco"