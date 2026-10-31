$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
New-Item -ItemType Directory -Force "$r\c512-prob\modules"|Out-Null
Copy-Item 'D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*' "$r\c512-prob\modules"
Copy-Item "$r\c512-prob-compile\c512-m32-mh.hsaco" "$r\c512-prob\modules\c512-m32-mh.hsaco"
