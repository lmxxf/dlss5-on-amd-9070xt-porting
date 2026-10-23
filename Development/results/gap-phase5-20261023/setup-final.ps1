$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\gap-phase5-20261023'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
New-Item -ItemType Directory -Force "$r\final\modules"|Out-Null
Copy-Item "$r\baseline\modules\*" "$r\final\modules"
Copy-Item "$r\knife1\compile-final-gfx1201\c512-m32-mh.hsaco" "$r\final\modules"
Get-ChildItem "$r\final\modules" -File | ForEach-Object {[PSCustomObject]@{path=$_.FullName;sha256=(Get-FileHash $_.FullName).Hash.ToLower()}} | ConvertTo-Json | Set-Content "$r\final\module-hashes.json"
'FINAL_MODULES_READY'
