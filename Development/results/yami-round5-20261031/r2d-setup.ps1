$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
foreach($arm in @('baseline','candidate')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*' "$r\$arm\modules"}
Copy-Item "$r\c32-wave1-fast.hsaco" "$r\candidate\modules\c32-wave1-fast.hsaco"
Copy-Item "$r\c32-wave1-fast-norm900.hsaco" "$r\candidate\modules\c32-wave1-fast-norm900.hsaco"
