$ErrorActionPreference='Stop'
if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Exit game before deploying'}
$Game='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
$Source='D:\DLSSNR-Lab\native-game-neural-tiled-explicit.addon64'
$Assets='D:\DLSSNR-Lab\native-game-tiled-assets'
$Baseline='D:\DLSSNR-Lab\native-color-frame-samegpu'
if((Get-FileHash $Source).Hash.ToLower() -ne '61e731057b39233b8e9c55cdd6d08791c6a7196d99d6cf44e2b812f18552b1cb'){throw 'Candidate DLL hash mismatch'}
foreach($File in Get-ChildItem $Baseline -File | Where-Object {$_.Extension -in '.f32','.i32','.hlsl','.hlsli' -and $_.Name -ne 'native_c64.hlsl'}){
 if((Get-FileHash $File.FullName).Hash -ne (Get-FileHash (Join-Path $Assets $File.Name)).Hash){throw "Asset mismatch: $($File.Name)"}
}
if((Get-FileHash "$Assets\native_c64.hlsl").Hash -ne (Get-FileHash 'D:\DLSSNR-Lab\native-network70-tiled\native_c64.hlsl').Hash){throw 'Shader differs from accepted network'}
$Target=Join-Path $Game 'native-submission-order.addon64'
$Backup="$Target.before-tiled-verification"
if(Test-Path $Backup){throw 'Backup already exists; inspect deployment instead of overwriting'}
Copy-Item $Target $Backup
Copy-Item $Source $Target -Force
if((Get-FileHash $Target).Hash -ne (Get-FileHash $Source).Hash){throw 'Installed hash mismatch'}
Get-FileHash $Target,$Backup
Write-Output 'Deployed diagnostic reset-history DLL; explicit PID/request required. Not temporal acceptance.'
