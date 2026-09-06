param([string]$Folder='D:\DLSSNR-Lab\native-vit-extent-256')
$ErrorActionPreference='Stop'
$env:DLSS5_VIT_CUBIN=Join-Path $Folder 'dlssnr-05.cubin'
foreach($Trial in 1..2){
 & (Join-Path $Folder 'native-vit-attention-oracle.exe') (Join-Path $Folder 'q.fp8') (Join-Path $Folder 'k.fp8') (Join-Path $Folder 'v.fp8') (Join-Path $Folder "rtx-output-$Trial.fp8") 16 16 32
 if($LASTEXITCODE -ne 0){throw "Original CUDA probe failed: $LASTEXITCODE"}
}
$First=(Get-FileHash (Join-Path $Folder 'rtx-output-1.fp8')).Hash
$Second=(Get-FileHash (Join-Path $Folder 'rtx-output-2.fp8')).Hash
Write-Output "first=$First second=$Second"
if($First -ne $Second){throw 'Identical input produced different outputs'}
