param([int]$Frame)
$ErrorActionPreference = 'Stop'
$L = 'D:\DLSSNR-Lab'
$env:DML_VIT_WEIGHT_DIR = $L
$env:DML_VIT8_OUTPUT = "$L\raw-$Frame-block38.f32"
$env:DML_BLOCK30_BODY = "$L\raw-$Frame-block30-body.f32"
$env:DML_BLOCK30_POOL = "$L\block30-pool-identity.bin"
$env:DML_BLOCK30_ENTER = "$L\block30-enter-512x1024.bin"
$env:DML_TRUE_EXPAND_WEIGHT = "$L\block31-vit-expand-effective.f16"
$env:DML_TRUE_CONTRACT_WEIGHT = "$L\block31-vit-contract.f16"
$env:DML_TRUE_CONTRACT_SKIP = "$L\block31-vit-contract-skip.f16"
$env:DML_TRUE_PROJECTION_WEIGHT = "$L\block31-vit-projection.f16"
$env:DML_TRUE_PROJECTION_SKIP = "$L\block31-vit-projection-skip.f16"
& "$L\d3d12_directml_vit_resident.exe"
if ($LASTEXITCODE) { throw "resident DirectML blocks31-38 failed: $LASTEXITCODE" }
