$ErrorActionPreference='Stop'
if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Game still running'}
$Probe='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64\native-present-contract.addon64'
$Backup=$Probe+'.disabled-api-audit'
if(Test-Path -LiteralPath $Backup){throw 'Backup exists; inspect first'}
Move-Item -LiteralPath $Probe -Destination $Backup
(Get-FileHash -LiteralPath $Backup).Hash
