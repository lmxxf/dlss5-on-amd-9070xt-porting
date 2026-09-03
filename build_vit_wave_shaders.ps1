param(
    [string]$Lab = 'D:\DLSSNR-Lab',
    [int]$Tokens = 2160
)
$ErrorActionPreference = 'Stop'
$Dxc = Join-Path $Lab 'dxc\bin\x64\dxc.exe'
if (!(Test-Path $Dxc)) { throw "DXC not found: $Dxc" }
$Output = Join-Path $Lab 'vit-attention-wave32.cso'
& $Dxc -T cs_6_6 -E attention_cs -O3 -D "TOKENS=$Tokens" `
    (Join-Path $Lab 'vit_attention_wave32.hlsl') -Fo $Output
if ($LASTEXITCODE) { throw 'Wave32 ViT attention compilation failed' }
Get-FileHash $Output -Algorithm SHA256 | Format-Table Path, Hash
