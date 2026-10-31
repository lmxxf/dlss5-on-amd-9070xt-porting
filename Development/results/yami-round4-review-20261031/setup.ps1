$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round4-review-20261031'
foreach($arm in @('baseline','candidate')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\ksearch-round4-20261031\payload\HIP\gfx1201\*' "$r\$arm\modules"}
Copy-Item "$r\compile\candidate\deep_fast-packed-fast.hsaco" "$r\candidate\modules\deep_fast-packed-fast.hsaco"
