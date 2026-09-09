param([switch]$Restore)
$ErrorActionPreference='Stop'
if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Exit game before changing its saved settings'}
$Settings=Join-Path $env:LOCALAPPDATA 'SB\Saved\Config\WindowsNoEditor\GameUserSettings.ini'
$SettingsBackup='D:\DLSSNR-Lab\matrix-probe\GameUserSettings-before-aa-repair.ini'
$Config=[IO.File]::ReadAllText($Settings)
if($Config -notmatch '(?m)^AntiAliasing=(SB_GAMEUSERSETTINGS_(?:OFF|HIGH))\r?$'){throw 'Unexpected AA setting; inspect before changing'}
$Target='SB_GAMEUSERSETTINGS_HIGH'
if($Restore){
 if(-not(Test-Path $SettingsBackup)){throw 'Missing original settings backup'}
 $Saved=[IO.File]::ReadAllText($SettingsBackup)
 if($Saved -notmatch '(?m)^AntiAliasing=(SB_GAMEUSERSETTINGS_\w+)\r?$'){throw 'Invalid backup AA setting'}
 $Target=$Matches[1]
}elseif(-not(Test-Path $SettingsBackup)){Copy-Item $Settings $SettingsBackup}
$Config=[regex]::Replace($Config,'(?m)^AntiAliasing=SB_GAMEUSERSETTINGS_\w+',('AntiAliasing='+$Target))
[IO.File]::WriteAllText($Settings,$Config,[Text.UTF8Encoding]::new($false))
Get-FileHash $Settings
