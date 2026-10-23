$ErrorActionPreference='Stop'
& 'D:\DLSSNR-Lab\game-check.ps1' 'Shipping SB-Win64 Onimusha re9.exe Magpie SandFall Wuthering Client-Win64'
if($LASTEXITCODE -ne 1){throw 'game-check not idle'}
if(Get-Process | Where-Object {$_.ProcessName -match '^(benchmark|nr_graph|rtc_compile|frame|frame-probe|integrated-probe)$'}){throw 'another GPU/RTC job'}
Get-PSDrive C,D | Select-Object Name,Free,Used | ConvertTo-Json
if((Get-PSDrive D).Free -lt 100GB){throw 'free space <100GB'}
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU lock exists'}
Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion | ConvertTo-Json
