$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\backfeed-20261028'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
$base='D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules'
if((Get-FileHash "$base\vit-stream-fast.hsaco").Hash.ToLower() -ne 'bf1dd632743a0792a76832e436229c1faee68999801a565e62c9f66145522836'){throw 'production identity drift'}
New-Item -ItemType Directory -Force "$r\knife2\qkv\modules"|Out-Null
Copy-Item "$base\*" "$r\knife2\qkv\modules"
Copy-Item "$r\knife2\compile-qkv\vit-stream-fast.hsaco" "$r\knife2\qkv\modules"
