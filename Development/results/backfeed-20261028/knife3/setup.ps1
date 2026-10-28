$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\backfeed-20261028'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
$base='D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules'
New-Item -ItemType Directory -Force "$r\knife3\candidate\modules"|Out-Null
Copy-Item "$base\*" "$r\knife3\candidate\modules"
Copy-Item "$r\c64-wave2.hsaco","$r\c64-wave2-fast.hsaco" "$r\knife3\candidate\modules"
