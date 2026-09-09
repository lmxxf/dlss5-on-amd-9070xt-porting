$ErrorActionPreference='Stop'
Get-Process | Where-Object {$_.ProcessName -match 'native-attention|SB-Win64'} | Select-Object Id,ProcessName | Format-Table
Get-WinEvent -FilterHashtable @{LogName='System';StartTime=(Get-Date).AddMinutes(-30)} -ErrorAction SilentlyContinue |
 Where-Object {$_.Id -eq 4101 -or $_.ProviderName -match 'Display|amdw|Dxg'} |
 Select-Object TimeCreated,Id,ProviderName,Message | Format-List
Get-WinEvent -FilterHashtable @{LogName='Application';StartTime=(Get-Date).AddMinutes(-30);Id=1001} -ErrorAction SilentlyContinue |
 Where-Object {$_.Message -match 'LiveKernelEvent|native-attention'} |
 Select-Object TimeCreated,Id,Message | Format-List
