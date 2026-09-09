param(
    [string]$Lab = 'D:\DLSSNR-Lab',
    [string]$Source = 'D:\DLSSNR-Lab\probe-block30-pad-native.f32',
    [ValidateRange(31, 38)][int]$Block = 31,
    [int]$Iterations = 100
)
$ErrorActionPreference = 'Stop'
function Run([string]$Name, [scriptblock]$Body) {
    & $Body
    if ($LASTEXITCODE) { throw "$Name failed: $LASTEXITCODE" }
}
$env:DML_GPU_BOUNDARY = '1'
$Prefix = "$Lab\block$Block-all-dml"
$ExpandWeight = "$Lab\block$Block-vit-expand-effective.f16"
$QkvWeight = "$Lab\block$Block-qkv-directml.f16"
$QkvScales = "$Lab\block$Block-qkv-scales.f32"
Remove-Item Env:DML_VIT_CONTRACT,Env:DML_VIT_PROJECTION,Env:DML_QKV_NORMALIZE,Env:DML_RAW_UNPACK -ErrorAction SilentlyContinue
Run 'Expand' { & "$Lab\d3d12_directml_gemm.exe" 2160 1024 4096 $Iterations $Source $ExpandWeight "$Prefix-expand.f32" }
$env:DML_VIT_CONTRACT = '1'
$env:DML_CONTRACT_RESIDUAL = $Source
$env:DML_CONTRACT_SKIP = "$Lab\block31-vit-contract-skip.f16"
Run 'Contract' { & "$Lab\d3d12_directml_gemm.exe" 2160 4096 1024 $Iterations "$Prefix-expand.f32" "$Lab\block31-vit-contract.f16" "$Prefix-hidden.f32" }
Remove-Item Env:DML_VIT_CONTRACT
$env:DML_QKV_NORMALIZE = '1'
$env:DML_QKV_SCALES = $QkvScales
Run 'QKV' { & "$Lab\d3d12_directml_gemm.exe" 2160 1024 3072 $Iterations "$Prefix-hidden.f32" $QkvWeight "$Prefix-qkv.f32" }
Remove-Item Env:DML_QKV_NORMALIZE,Env:DML_GPU_BOUNDARY
Run 'Attention' { & "$Lab\d3d12_directml_attention.exe" "$Prefix-qkv.f32" "$Prefix-attention.f32" }
$env:DML_GPU_BOUNDARY = '1'
$env:DML_VIT_PROJECTION = '1'
$env:DML_CONTRACT_RESIDUAL = "$Prefix-hidden.f32"
$env:DML_CONTRACT_SKIP = "$Lab\block31-vit-projection-skip.f16"
Run 'Projection' { & "$Lab\d3d12_directml_gemm.exe" 2160 1024 1024 $Iterations "$Prefix-attention.f32" "$Lab\block31-vit-projection.f16" "$Prefix-final.f32" }
Remove-Item Env:DML_GPU_BOUNDARY,Env:DML_VIT_PROJECTION,Env:DML_CONTRACT_RESIDUAL,Env:DML_CONTRACT_SKIP
