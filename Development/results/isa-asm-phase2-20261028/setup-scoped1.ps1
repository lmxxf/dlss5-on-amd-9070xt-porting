$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\isa-asm-phase2-20261028'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "$r\knife1\scoped-modules"|Out-Null
Copy-Item 'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules\*' "$r\knife1\scoped-modules"
Copy-Item "$r\knife1\compile-scoped-final-gfx1201\c512-m32-mh.hsaco" "$r\knife1\scoped-modules"
