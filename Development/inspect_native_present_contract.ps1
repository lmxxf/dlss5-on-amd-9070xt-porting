$ErrorActionPreference='Stop'
Get-CimInstance Win32_Process | Where-Object Name -Match 'SB|crs' | Select-Object Name,ProcessId | ConvertTo-Json
$Game='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
Select-String -LiteralPath "$Game\ReShade.log" -Pattern 'native-present|Present Contract|error' | Select-Object -Last 12 Line | ConvertTo-Json
Get-Content 'C:\Program Files (x86)\Steam\logs\gameprocess_log.txt' -Tail 8
if(Test-Path 'D:\DLSSNR-Lab\logs\native-present-contract.txt'){Get-Content 'D:\DLSSNR-Lab\logs\native-present-contract.txt' -Tail 8}
