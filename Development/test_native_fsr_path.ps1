param([switch]$Restore,[switch]$NoLaunch)
$ErrorActionPreference='Stop'
$Game=Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue
if($Game){Stop-Process -InputObject $Game -Force;if(-not $Game.WaitForExit(30000)){throw 'Game did not exit'}}
$Path=Join-Path $env:LOCALAPPDATA 'SB\Saved\Config\WindowsNoEditor\Engine.ini'
$Backup='D:\DLSSNR-Lab\matrix-probe\Engine-before-native-fsr-test.ini'
if($Restore){
 if(-not(Test-Path $Backup)){throw 'Missing original configuration'}
 Copy-Item $Backup $Path -Force
}else{
 if(-not(Test-Path $Backup)){Copy-Item $Path $Backup}
 $Original=[IO.File]::ReadAllText($Backup)
 if($Original -match '(?im)^\[SystemSettings\]'){throw 'Existing SystemSettings section requires manual merge'}
 $Test="`r`n[SystemSettings]`r`nr.FidelityFX.FSR3.Enabled=1`r`nr.FidelityFX.FSR3.UseNativeDX12=1`r`nr.FidelityFX.FSR3.UseRHI=0`r`nr.FidelityFX.FI.Enabled=0`r`n"
 [IO.File]::WriteAllText($Path,$Original+$Test,[Text.UTF8Encoding]::new($false))
}
Get-FileHash $Path
if(-not $NoLaunch){Start-ScheduledTask -TaskName 'DLSS5Launch'}
