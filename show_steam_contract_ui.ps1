$ErrorActionPreference='Stop'
$Steam=Get-Process steam -ErrorAction Stop | Select-Object -First 1
$Shell=New-Object -ComObject WScript.Shell
$Activated=$Shell.AppActivate($Steam.Id)
"steam_pid=$($Steam.Id) activated=$Activated" | Set-Content D:\DLSSNR-Lab\logs\steam-contract-focus.txt
Start-Sleep -Milliseconds 750
& D:\DLSSNR-Lab\tools\windows_ui_bridge.ps1 -Action Screenshot
