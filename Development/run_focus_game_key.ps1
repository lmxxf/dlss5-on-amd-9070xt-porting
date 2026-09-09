param([int]$VirtualKey = 13, [int]$HoldMilliseconds = 100, [switch]$Alt)
$Task = 'DLSSNR-Game-Key'
$UserId = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$AltArgument=if($Alt){' -Alt'}else{''}
$Action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File D:\DLSSNR-Lab\focus_game_key.ps1 -VirtualKey $VirtualKey -HoldMilliseconds $HoldMilliseconds$AltArgument"
$Principal = New-ScheduledTaskPrincipal -UserId $UserId -LogonType Interactive -RunLevel Highest
$Settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 1)
Unregister-ScheduledTask -TaskName $Task -Confirm:$false -ErrorAction SilentlyContinue
Register-ScheduledTask -TaskName $Task -Action $Action -Principal $Principal -Settings $Settings | Out-Null
Start-ScheduledTask -TaskName $Task
Start-Sleep -Seconds 2
$Info = Get-ScheduledTaskInfo -TaskName $Task
Unregister-ScheduledTask -TaskName $Task -Confirm:$false
Write-Output "Game key result: $($Info.LastTaskResult)"
