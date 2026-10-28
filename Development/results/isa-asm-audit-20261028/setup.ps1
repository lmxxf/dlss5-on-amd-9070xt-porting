$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\isa-asm-audit-20261028'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
foreach($role in @('upwide','dectail')){
 New-Item -ItemType Directory -Force "$r\$role\modules"|Out-Null
 Copy-Item 'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules\*' "$r\$role\modules"
}
Copy-Item "$r\c64-wave2-fast.hsaco" "$r\upwide\modules"
Copy-Item "$r\compile\dectail\deep_fast-packed-fast.hsaco" "$r\dectail\modules"
