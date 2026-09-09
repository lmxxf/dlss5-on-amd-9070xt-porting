param([int]$Frame)
$ErrorActionPreference = 'Stop'
$L = 'D:\DLSSNR-Lab'
$env:DML_SWIN512_WEIGHT_DIR = $L
$env:DML_SWIN512_OUTPUT = "$L\raw-$Frame-block47.f32"
$env:DML_BLOCK39_MAIN = "$L\raw-$Frame-block38.f32"
$env:DML_BLOCK39_SKIP = "$L\raw-$Frame-block30-body.f32"
$env:DML_BLOCK39_MATRIX = "$L\block39-directml-matrix.f16"
$env:DML_BLOCK39_BIAS = "$L\block39-directml-bias.f32"
Remove-Item Env:DML_SWIN512_INPUT -ErrorAction SilentlyContinue
& "$L\d3d12_directml_swin512_resident.exe"
if ($LASTEXITCODE) { throw "resident DirectML block39-47 failed: $LASTEXITCODE" }
