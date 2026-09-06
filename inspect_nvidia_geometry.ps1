$ErrorActionPreference='Stop'
Write-Output "user=$env:USERNAME localappdata=$env:LOCALAPPDATA"
Get-Process | Where-Object { $_.ProcessName -match 'SB|Stellar' } | Select-Object Id,ProcessName,MainWindowTitle
$Config=Join-Path $env:LOCALAPPDATA 'SB\Saved\Config'
Write-Output "config=$Config exists=$(Test-Path $Config)"
if(Test-Path $Config){
 Get-ChildItem $Config -Recurse -Filter GameUserSettings.ini | ForEach-Object {
  Write-Output "settings=$($_.FullName)"
  Get-Content $_.FullName | Select-String 'Resolution|Fullscreen|DesiredScreen|LastUserConfirmed'
 }
}
Get-ChildItem 'C:\Users' -Directory | Select-Object FullName
Get-ChildItem 'D:\DLSSNR-Lab\logs' -Directory -Filter 'native-kernel-params*' | Sort-Object LastWriteTime -Descending | Select-Object -First 3 Name,LastWriteTime
