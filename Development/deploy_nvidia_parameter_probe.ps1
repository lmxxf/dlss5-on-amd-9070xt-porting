param([Parameter(Mandatory=$true)][string]$ExpectedHash,[switch]$Launch)
$ErrorActionPreference='Stop'
$Source='D:\DLSSNR-Lab\preblock-live-parameters-next.addon64'
$Target='D:\SteamLibrary\steamapps\common\StellarBlade\SB\Binaries\Win64\preblock-live-parameters.addon64'
if((Get-FileHash $Source).Hash -ne $ExpectedHash){throw 'Probe source hash mismatch'}
if (@(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue).Count) { throw 'Exit Stellar Blade before installing the read-only probe' }
foreach($Launcher in @(Get-Process SB -ErrorAction SilentlyContinue)){
 if($Launcher.Path -eq 'D:\SteamLibrary\steamapps\common\StellarBlade\SB.exe'){throw 'Game launcher is still running'}
}
if(Test-Path -LiteralPath $Target){
 $OldHash=(Get-FileHash -LiteralPath $Target).Hash
 $Backup=$Target+'.backup-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
 if(Test-Path -LiteralPath $Backup){throw 'Backup path already exists'}
 Copy-Item -LiteralPath $Target -Destination $Backup
 if((Get-FileHash -LiteralPath $Backup).Hash -ne $OldHash){throw 'Probe backup hash mismatch'}
 Write-Output "backup=$Backup old_hash=$OldHash"
}
Copy-Item -LiteralPath $Source -Destination $Target -Force
if((Get-FileHash $Target).Hash -ne $ExpectedHash){throw 'Probe installed hash mismatch'}
Write-Output "installed=$ExpectedHash"
if($Launch){Start-ScheduledTask DLSSNR-Launch-StellarBlade}
