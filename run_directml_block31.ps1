param(
    [string]$Lab = 'D:\DLSSNR-Lab',
    [string]$Input = 'D:\DLSSNR-Lab\probe-block30-pad-native.f32',
    [int]$Iterations = 100
)
$ErrorActionPreference = 'Stop'
function Run([string]$Name, [scriptblock]$Body) {
    & $Body
    if ($LASTEXITCODE) { throw "$Name failed: $LASTEXITCODE" }
}
$env:DML_GPU_BOUNDARY = '1'
Remove-Item Env:DML_VIT_CONTRACT,Env:DML_VIT_PROJECTION,Env:DML_QKV_NORMALIZE,Env:DML_RAW_UNPACK -ErrorAction SilentlyContinue
Run 'Expand' { & "$Lab\d3d12_directml_gemm.exe" 2160 1024 4096 $Iterations $Input "$Lab\block31-vit-expand-effective.f16" "$Lab\block31-all-dml-expand.f32" }
$env:DML_VIT_CONTRACT = '1'
$env:DML_CONTRACT_RESIDUAL = $Input
$env:DML_CONTRACT_SKIP = "$Lab\block31-vit-contract-skip.f16"
Run 'Contract' { & "$Lab\d3d12_directml_gemm.exe" 2160 4096 1024 $Iterations "$Lab\block31-all-dml-expand.f32" "$Lab\block31-vit-contract.f16" "$Lab\block31-all-dml-hidden.f32" }
Remove-Item Env:DML_VIT_CONTRACT
$env:DML_QKV_NORMALIZE = '1'
$env:DML_QKV_SCALES = "$Lab\block31-qkv-scales.f32"
Run 'QKV' { & "$Lab\d3d12_directml_gemm.exe" 2160 1024 3072 $Iterations "$Lab\block31-all-dml-hidden.f32" "$Lab\block31-qkv-effective.f16" "$Lab\block31-all-dml-qkv.f32" }
Remove-Item Env:DML_QKV_NORMALIZE,Env:DML_GPU_BOUNDARY
Run 'Attention' { & "$Lab\d3d12_directml_attention.exe" "$Lab\block31-all-dml-qkv.f32" "$Lab\block31-all-dml-attention.f32" }
$env:DML_GPU_BOUNDARY = '1'
$env:DML_VIT_PROJECTION = '1'
$env:DML_CONTRACT_RESIDUAL = "$Lab\block31-all-dml-hidden.f32"
$env:DML_CONTRACT_SKIP = "$Lab\block31-vit-projection-skip.f16"
Run 'Projection' { & "$Lab\d3d12_directml_gemm.exe" 2160 1024 1024 $Iterations "$Lab\block31-all-dml-attention.f32" "$Lab\block31-vit-projection.f16" "$Lab\block31-all-dml-final.f32" }
Remove-Item Env:DML_GPU_BOUNDARY,Env:DML_VIT_PROJECTION,Env:DML_CONTRACT_RESIDUAL,Env:DML_CONTRACT_SKIP
