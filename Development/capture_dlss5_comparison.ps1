param([ValidateSet('menu','game')][string]$Scene='menu',[switch]$Schedule)
$ErrorActionPreference='Stop'
if($Schedule){
 $Action=New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File D:\DLSSNR-Lab\capture_dlss5_comparison.ps1 -Scene $Scene"
 $Principal=New-ScheduledTaskPrincipal -UserId 'lmxxf' -LogonType Interactive
 Register-ScheduledTask -TaskName 'DLSS5Comparison' -Action $Action -Principal $Principal -Force | Out-Null
 Start-ScheduledTask DLSS5Comparison
 exit
}
$Log='D:\DLSSNR-Lab\logs\dlss5-1080p-runtime.txt'
$Folder='D:\DLSSNR-Lab\logs\comparison'
[void][IO.Directory]::CreateDirectory($Folder)
function Assert-Mode([int]$Expected){
 $Tail=Get-Content $Log -Tail 180
 if(-not($Tail -match 'display_residual generation=')){throw 'No recent neural display progress'}
 $ModeLine=Get-Content $Log | Where-Object {$_ -match '^output_mode='} | Select-Object -Last 1
 $Mode=0;if($ModeLine -match '^output_mode=(\d)'){$Mode=[int]$Matches[1]}
 if($Mode -ne $Expected){throw "Expected mode $Expected; got $Mode"}
}
Assert-Mode 0
& D:\DLSSNR-Lab\capture_amd_desktop.ps1 -Output "$Folder\$Scene-on.png"
Get-Content $Log -Tail 30 | Set-Content "$Folder\$Scene-on-log.txt"
& D:\DLSSNR-Lab\capture_amd_desktop.ps1 -KeyCode 117 -Output "$Folder\$Scene-off.png"
Assert-Mode 1
Get-Content $Log -Tail 30 | Set-Content "$Folder\$Scene-off-log.txt"
# Restore neural mode, passing through split-screen mode 2.
& D:\DLSSNR-Lab\capture_amd_desktop.ps1 -KeyCode 117 -Output "$Folder\$Scene-split.png"
& D:\DLSSNR-Lab\capture_amd_desktop.ps1 -KeyCode 117 -Output "$Folder\$Scene-restored.png"
Assert-Mode 0
