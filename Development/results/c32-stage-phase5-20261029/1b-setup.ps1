$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\c32-stage-phase5-20261029'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','1b')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\isa-asm-phase3-20261029\payload\HIP\gfx1201\*' "$r\$arm\modules"}
Copy-Item "$r\payload\1b-c32-wave1-fast.hsaco" "$r\1b\modules\c32-wave1-fast.hsaco"
Copy-Item "$r\payload\1b-c32-wave1-fast-norm900.hsaco" "$r\1b\modules\c32-wave1-fast-norm900.hsaco"
