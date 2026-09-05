param([string]$Lab = 'D:\DLSSNR-Lab', [ValidateSet(1,2)][int]$Mode = 2, [ValidateSet(0,1)][int]$UnrollProject = 0, [ValidateSet(0,32,64)][int]$WaveSize = 0)
$ErrorActionPreference = 'Stop'
$Dxc = Join-Path $Lab 'dxc\bin\x64\dxc.exe'
$Target = if ($WaveSize) {'cs_6_6'} else {'cs_6_2'}
$Extra = if ($WaveSize) {@('-D',"WAVE_SIZE=$WaveSize")} else {@()}
& $Dxc -T $Target -E main -O3 -D "CACHE_SCORES=$Mode" -D "UNROLL_PROJECT=$UnrollProject" @Extra (Join-Path $Lab 'block70_attention_shared.hlsl') -Fo (Join-Path $Lab 'shader-cache\block70-cached-scores.cso')
if ($LASTEXITCODE) { throw 'Cached score attention compilation failed' }
