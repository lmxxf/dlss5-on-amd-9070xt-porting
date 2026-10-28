$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\isa-asm-phase3-20261029'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
foreach($role in @('knife2P','knife2S')){New-Item -ItemType Directory -Force "$r\$role\modules"|Out-Null;Copy-Item "$r\baseline\modules\*" "$r\$role\modules"}
Copy-Item "$r\knife2-pref.hsaco" "$r\knife2P\modules\c64-wave2-fast.hsaco"
Copy-Item "$r\knife2\compile-sp\swin-persistent.hsaco" "$r\knife2S\modules"
