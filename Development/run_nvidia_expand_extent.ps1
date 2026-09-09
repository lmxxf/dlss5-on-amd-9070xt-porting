param([string]$Folder='D:\DLSSNR-Lab\native-expand-256')
$ErrorActionPreference='Stop'
$env:DLSS5_VIT_CUBIN=Join-Path $Folder 'dlssnr-05.cubin'
foreach($Trial in 1..2){
 & (Join-Path $Folder 'native-expand.exe') (Join-Path $Folder 'input.fp8') (Join-Path $Folder "expand-$Trial.fp8") (Join-Path $Folder 'weights.bin') 256 64
 if($LASTEXITCODE -ne 0){throw "expand failed: $LASTEXITCODE"}
}
$A=(Get-FileHash (Join-Path $Folder 'expand-1.fp8')).Hash
$B=(Get-FileHash (Join-Path $Folder 'expand-2.fp8')).Hash
Write-Output "first=$A second=$B"
if($A -ne $B){throw 'expand replay differs'}
