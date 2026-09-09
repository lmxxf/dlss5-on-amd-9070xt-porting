param([string]$Lab = 'D:\DLSSNR-Lab')
$ErrorActionPreference = 'Stop'
$Dxc = Join-Path $Lab 'dxc\bin\x64\dxc.exe'
foreach ($Set in @(1,2)) {
    & $Dxc -T cs_6_2 -E main -O3 -D WIDTH=960 -D HEIGHT=544 -D SHIFTED=1 -D "WINDOW_SET=$Set" (Join-Path $Lab 'block70_attention_shared.hlsl') -Fo (Join-Path $Lab "shader-cache\c32-split-attention-$Set.cso")
    if ($LASTEXITCODE) { throw 'Split attention compilation failed' }
}
