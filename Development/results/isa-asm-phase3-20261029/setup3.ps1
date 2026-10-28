$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\isa-asm-phase3-20261029'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "$r\knife3C\modules"|Out-Null
Copy-Item "$r\baseline\modules\*" "$r\knife3C\modules"
Copy-Item "$r\knife3\compile-c512-m32-mh\c512-m32-mh.hsaco" "$r\knife3C\modules"
