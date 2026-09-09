param([string]$Lab='D:\DLSSNR-Lab')
$ErrorActionPreference='Stop'
if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Exit game to avoid GPU benchmark contention'}
$Runner=Join-Path $Lab 'matrix-probe\ffn_compare.exe'
$Baseline=Join-Path $Lab 'shader-cache\block1-v1-960x544-s0-ffn.cso'
$Candidate=Join-Path $Lab 'matrix-probe\c32_ffn_linalg.cso'
foreach($Block in @(2,3,4)){
    $Weight=if($Block -eq 4){'block4-body-effective.bin'}else{"block$Block-effective.bin"}
    $InputName='directml-ref-block'+($Block-1)+'-1080p.f32'
    "block=$Block"
    & $Runner (Join-Path $Lab $Weight) (Join-Path $Lab "matrix-probe\block$Block-ffn-matrices.f16") (Join-Path $Lab $InputName) $Baseline $Candidate
    if($LASTEXITCODE -ne 0){throw "Comparison failed: block $Block"}
}
