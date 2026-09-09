$ErrorActionPreference='Stop'
$Game='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Exit game before changing addons'}
$Old=Join-Path $Game 'dlss5-1080p-runtime.addon64'
$Backup=$Old+'.before-order-probe'
$Source='D:\DLSSNR-Lab\native-submission-order.addon64'
$Target=Join-Path $Game 'native-submission-order.addon64'
if(!(Test-Path $Old -PathType Leaf) -or !(Test-Path $Source -PathType Leaf)){throw 'Missing deployment input'}
if((Test-Path $Backup) -or (Test-Path $Target)){throw 'Existing deployment; inspect instead of overwriting'}
$OldHash=(Get-FileHash $Old).Hash
Move-Item -LiteralPath $Old -Destination $Backup
try {Copy-Item -LiteralPath $Source -Destination $Target}
catch {Move-Item -LiteralPath $Backup -Destination $Old;throw}
@{disabled=$Backup;disabled_sha256=$OldHash;installed=$Target;installed_sha256=(Get-FileHash $Target).Hash;scope='read-only command order observation, neural patch disabled'} | ConvertTo-Json | Set-Content D:\DLSSNR-Lab\order-probe-deployment.json
Write-Output 'Original patch preserved; read-only order probe installed.'
