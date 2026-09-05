param([string]$Lab = 'D:\DLSSNR-Lab')
$ErrorActionPreference = 'Stop'
$Dxc = Join-Path $Lab 'dxc\bin\x64\dxc.exe'
foreach ($Width in @(960,1920)) {
    $Height = if ($Width -eq 960) {544} else {1088}
    & $Dxc -T cs_6_2 -E ffn -O3 -D BIT_QUANT=1 -D "WIDTH=$Width" -D "HEIGHT=$Height" (Join-Path $Lab 'block1_ffn_sm6_fp32.hlsl') -Fo (Join-Path $Lab "shader-cache\c32-bit-quant-$Width.cso")
    if ($LASTEXITCODE) { throw 'Bit quant FFN compilation failed' }
}
