$ErrorActionPreference='Stop'
if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Game is running; not touching assets or DLL'}
$Assets='D:\DLSSNR-Lab\native-game-tiled-assets';$Src='D:\DLSSNR-Lab\native-network70-shared-scratch'
$Backup="$Assets.shaders-before-fast-20260908"
if(!(Test-Path $Backup)){New-Item -ItemType Directory $Backup | Out-Null;Get-ChildItem $Assets -File | Where-Object {$_.Extension -in '.cso','.hlsl','.hlsli'} | Copy-Item -Destination $Backup}
$n=0;Get-ChildItem $Src -File | Where-Object {$_.Extension -in '.cso','.hlsl','.hlsli'} | ForEach-Object {Copy-Item $_.FullName (Join-Path $Assets $_.Name) -Force;$n++}
"shaders synced: $n (backup: $Backup)"
Copy-Item 'D:\DLSSNR-Lab\native-game-flags.txt' 'D:\DLSSNR-Lab\native-game-flags.txt' -ErrorAction SilentlyContinue
Set-Content 'D:\DLSSNR-Lab\enable-game-sdk721.txt' 'sdk721'
$Game='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
$Cur=Join-Path $Game 'native-submission-order.addon64'
$Bak=Join-Path $Game 'native-submission-order.addon64.before-fast-20260908'
if(!(Test-Path $Bak)){Copy-Item $Cur $Bak}
Copy-Item 'D:\DLSSNR-Lab\native-game-fast.addon64' $Cur -Force
'installed: '+(Get-FileHash $Cur -Algorithm SHA256).Hash
'backup:    '+(Get-FileHash $Bak -Algorithm SHA256).Hash
