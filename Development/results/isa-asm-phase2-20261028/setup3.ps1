$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\isa-asm-phase2-20261028'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
foreach($role in @('knife3-pool','knife3-split')){New-Item -ItemType Directory -Force "$r\$role\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules\*' "$r\$role\modules"}
Copy-Item "$r\knife3\compile-pool-mask\multihead-fast-padded-wave-packed.hsaco" "$r\knife3-pool\modules"
Copy-Item "$r\knife3\compile-split\deep_fast-packed-fast.hsaco" "$r\knife3-split\modules"
