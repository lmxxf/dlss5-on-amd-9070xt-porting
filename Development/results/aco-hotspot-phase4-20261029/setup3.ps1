$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\aco-hotspot-phase4-20261029';$source='D:\DLSSNR-Lab\isa-asm-phase3-20261029\payload\HIP\gfx1201'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($role in @('baseline','3a','3b')){New-Item -ItemType Directory -Force "$r\$role\modules"|Out-Null;Copy-Item "$source\*" "$r\$role\modules"}
foreach($role in @('3a','3b')){Copy-Item "$r\payload\$role-c64-wave2-fast.hsaco" "$r\$role\modules\c64-wave2-fast.hsaco"}
