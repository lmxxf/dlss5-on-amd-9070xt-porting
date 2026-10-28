$ErrorActionPreference='Stop'
& 'D:\DLSSNR-Lab\game-check.ps1' 'Shipping SB-Win64 Onimusha re9.exe Magpie SandFall Wuthering Client-Win64 Genshin YuanShen' | Out-Host
if($LASTEXITCODE -ne 1){throw 'game running or check failed'}
$free=(Get-PSDrive D).Free
if($free -lt 100GB){throw 'free disk below 100GB'}
$jobs=@(Get-Process | Where-Object {$_.ProcessName -match '^(benchmark.*|candidate.*|trace|rtc_compile.*|frame.*|probe.*|pure.*|final-frame.*)$'})
if($jobs.Count){$jobs | Format-Table Id,ProcessName;throw 'experiment process exists'}
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'gpu.lock already exists'}
Write-Output "PREFLIGHT_IDLE disk_bytes=$free"
