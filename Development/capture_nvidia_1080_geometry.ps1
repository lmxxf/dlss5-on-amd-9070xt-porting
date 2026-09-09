param([switch]$Launch)
$ErrorActionPreference='Stop'
if(Get-Process | Where-Object {$_.ProcessName -match '^SB($|-)|StellarBlade'}){throw 'Game running; no configuration changes made'}
$Path=Join-Path $env:LOCALAPPDATA 'SB\Saved\Config\WindowsNoEditor\GameUserSettings.ini'
if(!(Test-Path $Path)){throw 'Missing actual game settings'}
$Task=Get-ScheduledTask -TaskName 'DLSSNR-Launch-StellarBlade'
if($Task.Actions.Execute -notmatch 'steam.exe$' -or $Task.Actions.Arguments -notmatch '3489700'){throw 'Unexpected launch task'}
$Text=[IO.File]::ReadAllText($Path)
$Updates=@{ResolutionSizeX='1920';ResolutionSizeY='1080';FullscreenMode='2';PreferredFullscreenMode='2'}
foreach($Key in $Updates.Keys){
 $Pattern='(?m)^'+[regex]::Escape($Key)+'=[^\r\n]*'
 if([regex]::Matches($Text,$Pattern).Count -ne 1){throw "Ambiguous/missing setting $Key"}
 $Text=[regex]::Replace($Text,$Pattern,($Key+'='+$Updates[$Key]))
}
$Backup=$Path+'.before-1080-probe-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')
Copy-Item -LiteralPath $Path -Destination $Backup
if((Get-FileHash $Path).Hash -ne (Get-FileHash $Backup).Hash){throw 'Backup mismatch'}
[IO.File]::WriteAllText($Path,$Text,(New-Object Text.UTF8Encoding($false)))
Write-Output "backup=$Backup"
Get-Content $Path | Select-String '^(ResolutionSizeX|ResolutionSizeY|FullscreenMode|PreferredFullscreenMode)='
if($Launch){Start-ScheduledTask -TaskName $Task.TaskName;Write-Output 'Launch requested; capture not yet verified'}
