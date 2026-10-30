$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-step4-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','k6b')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\c32-stage-phase5b-20261029\payload\HIP\gfx1201\*' "$r\$arm\modules"}
