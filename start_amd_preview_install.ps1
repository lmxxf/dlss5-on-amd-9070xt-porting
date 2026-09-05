$ErrorActionPreference='Stop'
$Name='DLSS5AMDPreviewInstall'
if(Get-ScheduledTask -TaskName $Name -ErrorAction SilentlyContinue){throw 'Existing installer task: inspect it instead of restarting'}
$Action=New-ScheduledTaskAction -Execute 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -Argument '-NoProfile -ExecutionPolicy Bypass -File D:\DLSSNR-Lab\install_amd_preview.ps1'
$Principal=New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
Register-ScheduledTask -TaskName $Name -Action $Action -Principal $Principal -Description 'User-authorized one-shot AMD preview driver install; no automatic reboot' | Out-Null
Start-ScheduledTask -TaskName $Name
Get-ScheduledTask -TaskName $Name | Select-Object TaskName,State
