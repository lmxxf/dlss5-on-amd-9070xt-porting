param([string]$Lab = 'D:\DLSSNR-Lab')
$ErrorActionPreference = 'Stop'
$Compiler = Join-Path $Lab 'dxc\bin\x64\dxc.exe'
$Source = Join-Path $Lab 'block70_ffn_sm6_fp32.hlsl'
$Output = Join-Path $Lab 'shader-cache\block70-1920x1088-ffn.cso'
& $Compiler -T cs_6_2 -E ffn -O3 -D WIDTH=1920 -D HEIGHT=1088 $Source -Fo $Output
if ($LASTEXITCODE) { throw 'Block70 SM6 compilation failed' }
Get-FileHash -LiteralPath $Output -Algorithm SHA256
$QkvOutput = Join-Path $Lab 'shader-cache\block70-parallel-qkv.cso'
& $Compiler -T cs_6_2 -E main -O3 (Join-Path $Lab 'block70_qkv_parallel.hlsl') -Fo $QkvOutput
if ($LASTEXITCODE) { throw 'Block70 parallel QKV compilation failed' }
Get-FileHash -LiteralPath $QkvOutput -Algorithm SHA256
$C32Output = Join-Path $Lab 'shader-cache\c32-960x544-parallel-qkv.cso'
& $Compiler -T cs_6_2 -E main -O3 -D WIDTH=960 -D HEIGHT=544 (Join-Path $Lab 'block70_qkv_parallel.hlsl') -Fo $C32Output
if ($LASTEXITCODE) { throw 'C32 parallel QKV compilation failed' }
Get-FileHash -LiteralPath $C32Output -Algorithm SHA256
& $Compiler -T cs_6_2 -E main -O3 -D WIDTH=960 -D HEIGHT=544 (Join-Path $Lab 'c32_ffn_group.hlsl') -Fo (Join-Path $Lab 'shader-cache\c32-960x544-group-ffn.cso')
if ($LASTEXITCODE) { throw 'C32 group FFN compilation failed' }
& $Compiler -T cs_6_2 -E main -O3 -D WIDTH=1920 -D HEIGHT=1088 -D TILED_INPUT=1 (Join-Path $Lab 'c32_ffn_group.hlsl') -Fo (Join-Path $Lab 'shader-cache\block70-group-ffn.cso')
if ($LASTEXITCODE) { throw 'Block70 group FFN compilation failed' }
