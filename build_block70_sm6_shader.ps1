param([string]$Lab='D:\DLSSNR-Lab')
$ErrorActionPreference='Stop'
$Dxc=Join-Path $Lab 'dxc\bin\x64\dxc.exe'
$Output=Join-Path $Lab 'shader-cache\block1-v1-3840x2160-s0-t1-ffn.cso'
& $Dxc -T cs_6_2 -E ffn -O3 (Join-Path $Lab 'block70_ffn_sm6_fp32.hlsl') -Fo $Output
if($LASTEXITCODE){throw 'block70 SM6 FFN compilation failed'}
Get-FileHash $Output -Algorithm SHA256 | Format-Table Path,Hash
