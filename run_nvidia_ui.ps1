param([ValidateSet('Screenshot','Key','Click')][string]$Action='Screenshot',[int]$VirtualKey=0,[int]$X=0,[int]$Y=0,[switch]$Interactive)
$ErrorActionPreference='Stop'
if($Interactive){
 $Game=Get-Process SB-Win64-Shipping -ErrorAction Stop
 $Shell=New-Object -ComObject WScript.Shell
 if(-not $Shell.AppActivate($Game.Id)){throw 'Could not focus game; refusing input'}
 Start-Sleep -Milliseconds 750
 & D:\DLSSNR-Lab\tools\windows_ui_bridge.ps1 -Action $Action -VirtualKey $VirtualKey -X $X -Y $Y
 exit
}
$Principal=(Get-ScheduledTask 'DLSSNR-UI').Principal
$TaskAction=New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File D:\DLSSNR-Lab\tools\run_nvidia_ui.ps1 -Interactive -Action $Action -VirtualKey $VirtualKey -X $X -Y $Y"
Register-ScheduledTask -TaskName 'DLSSNR-CompareUI' -Action $TaskAction -Principal $Principal -Force | Out-Null
Start-ScheduledTask 'DLSSNR-CompareUI'
