param([string]$Case)
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-round3-20261031'
& 'D:\DLSSNR-Lab\game-check.ps1' 'Shipping SB-Win64 Onimusha re9.exe Magpie SandFall Wuthering Client-Win64 Genshin YuanShen'|Out-Host
if($LASTEXITCODE -ne 1){throw 'not idle'}
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'locked'}
if(Get-Process|Where-Object {$_.ProcessName -match '^(frame.*|[234]-profile|final-profile|runtime-smoke|mochi-native|trace|rtc_compile.*|pure.*|benchmark.*)$'}){throw 'experiment active'}
$removed=@();Get-ChildItem "$r\$Case" -Recurse -File|Where-Object {$_.Extension -in '.f16','.ppm','.hsaco','.exe','.rgb32f','.rgba32f'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item -LiteralPath $_.FullName}
@{case=$Case;removed=$removed;gpu_lock_present=(Test-Path 'D:\DLSSNR-Lab\gpu.lock');D_free_bytes=(Get-PSDrive D).Free}|ConvertTo-Json -Depth 5|Set-Content "$r\$Case-cleanup.json"
