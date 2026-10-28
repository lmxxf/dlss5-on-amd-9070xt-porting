$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\isa-asm-audit-20261028'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
New-Item -ItemType Directory -Force "$r\ffnb2\modules"|Out-Null
Copy-Item 'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules\*' "$r\ffnb2\modules"
Copy-Item "$r\ffnb2.hsaco" "$r\ffnb2\modules\c64-wave2-fast.hsaco"
