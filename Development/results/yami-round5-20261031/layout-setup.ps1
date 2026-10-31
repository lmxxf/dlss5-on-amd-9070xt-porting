$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\layout0\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\layout0\modules"
Copy-Item "$r\pad0.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\layout0\modules\deep_fast-packed-fast.hsaco"
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\layout512\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\layout512\modules"
Copy-Item "$r\pad512.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\layout512\modules\deep_fast-packed-fast.hsaco"
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\layout1024\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\layout1024\modules"
Copy-Item "$r\pad1024.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\layout1024\modules\deep_fast-packed-fast.hsaco"
New-Item -ItemType Directory -Force "D:\DLSSNR-Lab\yami-round5-20261031\layout1792\modules"|Out-Null
Copy-Item "D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*" "D:\DLSSNR-Lab\yami-round5-20261031\layout1792\modules"
Copy-Item "$r\pad1792.hsaco" "D:\DLSSNR-Lab\yami-round5-20261031\layout1792\modules\deep_fast-packed-fast.hsaco"