$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\gap-phase6-20261023'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
if((Get-PSDrive D).Free -lt 100GB){throw 'disk below 100GB'}
New-Item -ItemType Directory -Force "$r\baseline\modules","$r\knife1","$r\knife2"|Out-Null
Copy-Item 'D:\DLSSNR-Lab\gap-phase5-20261023\final\modules\*' "$r\baseline\modules"
Copy-Item 'D:\DLSSNR-Lab\gap-phase5-20261023\knife2\final-frame-standard.exe' "$r\baseline\frame.exe"
Copy-Item 'D:\DLSSNR-Lab\gap-phase5-20261023\knife2\final-frame-free1072.exe' "$r\baseline\frame-free1072.exe"
'PHASE5_BASELINE_READY'
