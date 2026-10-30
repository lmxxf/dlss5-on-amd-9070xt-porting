$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-step3-20261030'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','k4')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\c32-stage-phase5b-20261029\payload\HIP\gfx1201\*' "$r\$arm\modules"}
Copy-Item "$r\payload\k4-swin-persistent.hsaco" "$r\k4\modules\swin-persistent.hsaco"
Copy-Item "$r\payload\k4-swin-persistent-fast.hsaco" "$r\k4\modules\swin-persistent-fast.hsaco"
