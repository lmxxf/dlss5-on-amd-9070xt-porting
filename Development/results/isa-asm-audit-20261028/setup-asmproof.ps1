$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\isa-asm-audit-20261028'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "$r\asmproof\modules"|Out-Null
Copy-Item 'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules\*' "$r\asmproof\modules"
Copy-Item "$r\asmproof.hsaco" "$r\asmproof\modules\c32-wave1-fast.hsaco"
