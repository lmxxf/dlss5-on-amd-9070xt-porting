param([string]$Lab = 'D:\DLSSNR-Lab')
$ErrorActionPreference = 'Stop'
$Compiler = Join-Path $Lab 'dxc\bin\x64\dxc.exe'
$Source = Join-Path $Lab 'block70_ffn_sm6_fp32.hlsl'
$Output = Join-Path $Lab 'shader-cache\block70-1920x1088-ffn.cso'
& $Compiler -T cs_6_2 -E ffn -O3 -D WIDTH=1920 -D HEIGHT=1088 $Source -Fo $Output
if ($LASTEXITCODE) { throw 'Block70 SM6 compilation failed' }
Get-FileHash -LiteralPath $Output -Algorithm SHA256
