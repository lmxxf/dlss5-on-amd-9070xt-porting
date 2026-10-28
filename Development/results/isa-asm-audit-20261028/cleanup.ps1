$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\isa-asm-audit-20261028'
& 'D:\DLSSNR-Lab\game-check.ps1' 'Shipping SB-Win64 Onimusha re9.exe Magpie SandFall Wuthering Client-Win64 Genshin YuanShen'|Out-Host
if($LASTEXITCODE -ne 1){throw 'game check not idle'}
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU lock present'}
if(Get-Process|Where-Object {$_.ProcessName -match '^(frame.*|trace|rtc_compile.*|runtime-smoke|pure.*|benchmark.*)$'}){throw 'experiment process present'}
$removed=@()
Get-ChildItem $r -Recurse -File | Where-Object {$_.Extension -in '.f16','.ppm','.rgb32f','.rgba32f','.hsaco','.exe','.hip','.o','.bc','.zip'} | ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item -LiteralPath $_.FullName}
if(Test-Path "$r\cache"){Remove-Item "$r\cache" -Recurse -Force}
@{removed=$removed;gpu_lock_present=(Test-Path 'D:\DLSSNR-Lab\gpu.lock');experiment_processes=@(Get-Process|Where-Object {$_.ProcessName -match '^(frame.*|trace|rtc_compile.*|pure.*|benchmark.*)$'}|Select-Object Id,ProcessName);D_free_bytes=(Get-PSDrive D).Free;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 5|Set-Content "$r\cleanup.json"
'CLEANUP_DONE'
