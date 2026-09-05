param([string]$Lab = 'D:\DLSSNR-Lab', [ValidateSet(1,2)][int]$Mode = 2)
$ErrorActionPreference = 'Stop'
$Dxc = Join-Path $Lab 'dxc\bin\x64\dxc.exe'
& $Dxc -T cs_6_2 -E main -O3 -D "CACHE_SCORES=$Mode" (Join-Path $Lab 'block70_attention_shared.hlsl') -Fo (Join-Path $Lab 'shader-cache\block70-cached-scores.cso')
if ($LASTEXITCODE) { throw 'Cached score attention compilation failed' }
