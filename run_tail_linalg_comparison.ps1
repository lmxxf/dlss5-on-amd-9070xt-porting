param([string]$Lab='D:\DLSSNR-Lab')
$ErrorActionPreference='Stop'
if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Exit game before isolated GPU comparison'}
$Root=Join-Path $Lab 'matrix-probe'
foreach($Block in @(66,67,68,69,70)){
    $Source=if($Block -eq 66){'raw-16800-block66-prefix.f32'}elseif($Block -eq 70){'raw-16800-block70-prefix-general.f32'}else{'raw-16800-block'+($Block-1)+'.f32'}
    [long]$Bytes=if($Block -eq 70){267386880}else{66846720}
    $Sample=Join-Path $Root "block$Block-input-sample.f32"
    $Reader=[IO.File]::OpenRead((Join-Path $Lab $Source))
    if($Reader.Length -lt $Bytes){$Reader.Dispose();throw 'Source too short'}
    $Writer=[IO.File]::Create($Sample)
    try{
        $Buffer=New-Object byte[] 1048576
        [long]$Remaining=$Bytes
        while($Remaining -gt 0){$N=$Reader.Read($Buffer,0,[int][Math]::Min($Remaining,$Buffer.Length));if($N -le 0){throw 'Unexpected EOF'};$Writer.Write($Buffer,0,$N);$Remaining-=$N}
    }finally{$Reader.Dispose();$Writer.Dispose()}
    # Operator-only samples: preserve real channel vectors, not a reconstructed 1080p scene.
    $Weight=if($Block -eq 70){'block70-body-compatible.bin'}else{"block$Block-body-effective.bin"}
    $Shape=if($Block -eq 70){'1920x1088'}else{'960x544'}
    $Candidate=if($Block -eq 70){'c32_ffn_linalg-1920.cso'}else{'c32_ffn_linalg.cso'}
    "block=$Block source=$Source sample_bytes=$Bytes"
    & (Join-Path $Root 'ffn_compare.exe') (Join-Path $Lab $Weight) (Join-Path $Root "block$Block-ffn-matrices.f16") $Sample (Join-Path $Lab "shader-cache\block1-v1-$Shape-s0-ffn.cso") (Join-Path $Root $Candidate)
    if($LASTEXITCODE -ne 0){throw "Comparison failed for $Block"}
}
