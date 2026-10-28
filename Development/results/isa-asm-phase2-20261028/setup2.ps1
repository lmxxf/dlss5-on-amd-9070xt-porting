$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\isa-asm-phase2-20261028'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "$r\knife2\modules"|Out-Null
Copy-Item 'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules\*' "$r\knife2\modules"
Copy-Item "$r\knife2\compile-expand\deep_fast-packed-fast.hsaco" "$r\knife2\modules"
