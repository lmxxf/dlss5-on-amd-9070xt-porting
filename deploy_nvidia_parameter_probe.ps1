param([Parameter(Mandatory=$true)][string]$ExpectedHash)
$ErrorActionPreference='Stop'
$Source='D:\DLSSNR-Lab\preblock-live-parameters.addon64'
$Target='D:\SteamLibrary\steamapps\common\StellarBlade\SB\Binaries\Win64\preblock-live-parameters.addon64'
if((Get-FileHash $Source).Hash -ne $ExpectedHash){throw 'Probe source hash mismatch'}
foreach($Game in @(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue)){
 Stop-Process -InputObject $Game -Force
 if(-not $Game.WaitForExit(30000)){throw 'Game did not exit'}
}
foreach($Launcher in @(Get-Process SB -ErrorAction SilentlyContinue)){
 if($Launcher.Path -eq 'D:\SteamLibrary\steamapps\common\StellarBlade\SB.exe' -and -not $Launcher.WaitForExit(10000)){
  Stop-Process -InputObject $Launcher -Force
  if(-not $Launcher.WaitForExit(10000)){throw 'Game launcher did not exit'}
 }
}
Start-Sleep -Seconds 10
Copy-Item $Source $Target -Force
if((Get-FileHash $Target).Hash -ne $ExpectedHash){throw 'Probe installed hash mismatch'}
Write-Output "installed=$ExpectedHash"
Start-ScheduledTask DLSSNR-Launch-StellarBlade
