$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\gap-phase6-20261023'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
New-Item -ItemType Directory -Force "$r\knife2\half-modules"|Out-Null
Copy-Item "$r\knife1\modules\*" "$r\knife2\half-modules"
Copy-Item "$r\knife2\half-compile-gfx1201\deep_fast-packed-fast.hsaco" "$r\knife2\half-modules"
'HALF_ONLY_MODULES_READY'
