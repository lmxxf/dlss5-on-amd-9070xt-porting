$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
New-Item -ItemType Directory -Force "$r\prob-direct\modules"|Out-Null
Copy-Item 'D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*' "$r\prob-direct\modules"
Copy-Item "$r\prob-c32-wave1-fast.hsaco" "$r\prob-direct\modules\c32-wave1-fast.hsaco"
Copy-Item "$r\prob-c32-wave1-fast-norm900.hsaco" "$r\prob-direct\modules\c32-wave1-fast-norm900.hsaco"
