$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\gap-phase5-20261023'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
New-Item -ItemType Directory -Force "$r\knife1","$r\knife2","$r\baseline\modules"|Out-Null
Copy-Item 'D:\DLSSNR-Lab\gap-phase4-20261023\knife1\modules\*' "$r\baseline\modules"
Copy-Item 'D:\DLSSNR-Lab\gap-phase4-20261023\knife2\final-frame-standard.exe' "$r\baseline\frame.exe"
Copy-Item 'D:\DLSSNR-Lab\gap-phase4-20261023\knife2\final-frame-free1072.exe' "$r\baseline\frame-free1072.exe"
Get-ChildItem "$r\baseline\modules" -File | ForEach-Object { [PSCustomObject]@{name=$_.Name;sha256=(Get-FileHash $_.FullName).Hash.ToLower()} } | ConvertTo-Json | Set-Content "$r\baseline\module-hashes.json"
'BASELINE_READY'
