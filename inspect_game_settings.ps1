param([switch]$Schedule)
$ErrorActionPreference='Stop'
if($Schedule){
 $Action=New-ScheduledTaskAction -Execute 'powershell.exe' -Argument '-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File D:\DLSSNR-Lab\inspect_game_settings.ps1'
 $Principal=New-ScheduledTaskPrincipal -UserId 'lmxxf' -LogonType Interactive
 Register-ScheduledTask -TaskName 'DLSS5SettingsInspect' -Action $Action -Principal $Principal -Force | Out-Null
 Start-ScheduledTask DLSS5SettingsInspect
 exit
}
# From Reflex, inspect AMD FSR quality without changing its value.
& D:\DLSSNR-Lab\capture_amd_desktop.ps1 -KeyCode 40
