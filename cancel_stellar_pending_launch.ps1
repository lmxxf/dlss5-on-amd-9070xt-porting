param([switch]$Schedule)
$ErrorActionPreference='Stop'
if($Schedule){
 $Principal=(Get-ScheduledTask DLSSNR-UI).Principal
 $Action=New-ScheduledTaskAction -Execute powershell.exe -Argument '-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File D:\DLSSNR-Lab\cancel_stellar_pending_launch.ps1'
 Register-ScheduledTask -TaskName DLSSNR-CancelPendingLaunch -Action $Action -Principal $Principal -Force | Out-Null
 Start-ScheduledTask DLSSNR-CancelPendingLaunch
 exit
}
if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Game exists; refusing pending-launch cancellation'}
# Coordinate checked on the current 3840x2160 Steam library screenshot.
& D:\DLSSNR-Lab\tools\windows_ui_bridge.ps1 -Action Click -X 600 -Y 648
