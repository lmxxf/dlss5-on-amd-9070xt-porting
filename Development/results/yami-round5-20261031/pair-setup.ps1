$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
New-Item -ItemType Directory -Force "$r\pair-scope\modules"|Out-Null
Copy-Item 'D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*' "$r\pair-scope\modules"
Copy-Item "$r\pair-final-compile\deep_fast-packed-fast.hsaco" "$r\pair-scope\modules\deep_fast-packed-fast.hsaco"
