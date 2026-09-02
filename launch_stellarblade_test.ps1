$ErrorActionPreference = 'Stop'

$TaskName = 'DLSSNR-StellarBlade-Test'
$Launcher = "$env:ComSpec"
$UserId = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value

$action = New-ScheduledTaskAction -Execute $Launcher -Argument '/c start "" "steam://rungameid/3489700"'
$principal = New-ScheduledTaskPrincipal `
    -UserId $UserId `
    -LogonType Interactive `
    -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Hours 4)

Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
Register-ScheduledTask `
    -TaskName $TaskName `
    -Action $action `
    -Principal $principal `
    -Settings $settings | Out-Null

Start-ScheduledTask -TaskName $TaskName
Start-Sleep -Seconds 5
$task = Get-ScheduledTask -TaskName $TaskName
$info = Get-ScheduledTaskInfo -TaskName $TaskName
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
Write-Output "Steam URI task state: $($task.State); result: $($info.LastTaskResult)"
