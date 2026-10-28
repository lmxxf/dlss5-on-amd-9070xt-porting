$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\isa-asm-phase3-20261029'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "$r\baseline\modules"|Out-Null
Copy-Item 'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules\*' "$r\baseline\modules"
foreach($role in @('knife1A','knife1B')){New-Item -ItemType Directory -Force "$r\$role\modules"|Out-Null;Copy-Item "$r\baseline\modules\*" "$r\$role\modules"}
Copy-Item "$r\knife1\compile-pair\c512-m32-deep.hsaco" "$r\knife1A\modules"
Copy-Item "$r\knife1\compile-bounded\c512-m32-deep.hsaco" "$r\knife1B\modules"
