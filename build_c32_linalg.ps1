param([string]$Lab='D:\DLSSNR-Lab',[ValidateSet(960,1920)][int]$Width=960)
$ErrorActionPreference='Stop'
$Root=Join-Path $Lab 'matrix-probe'
$Dxc=Join-Path $Root 'dxc-preview\bin\x64\dxc.exe'
$Height=if($Width -eq 960){544}else{1088}
$Output=if($Width -eq 960){'c32_ffn_linalg.cso'}else{'c32_ffn_linalg-1920.cso'}
& $Dxc -T cs_6_10 -HV 2021 -E main -O3 -enable-16bit-types -D "WIDTH=$Width" -D "HEIGHT=$Height" -I (Join-Path $Root 'dxc-preview\inc\hlsl') (Join-Path $Root 'c32_ffn_linalg.hlsl') -Fo (Join-Path $Root $Output)
if($LASTEXITCODE -ne 0){throw 'LinAlg FFN shader compilation failed'}
Get-FileHash -LiteralPath (Join-Path $Root $Output) -Algorithm SHA256 | Format-List
