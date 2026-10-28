$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\isa-asm-phase3-20261029'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "$r\knife1M\modules"|Out-Null
Copy-Item "$r\baseline\modules\*" "$r\knife1M\modules"
Copy-Item "$r\knife1\compile-mask\c512-m32-deep.hsaco" "$r\knife1M\modules"
