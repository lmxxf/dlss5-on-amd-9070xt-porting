$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-step3-20261030'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','k3')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\c32-stage-phase5b-20261029\payload\HIP\gfx1201\*' "$r\$arm\modules"}
Copy-Item "$r\payload\k3-c32-wave1-fast.hsaco" "$r\k3\modules\c32-wave1-fast.hsaco"
Copy-Item "$r\payload\k3-c32-wave1-fast-norm900.hsaco" "$r\k3\modules\c32-wave1-fast-norm900.hsaco"
