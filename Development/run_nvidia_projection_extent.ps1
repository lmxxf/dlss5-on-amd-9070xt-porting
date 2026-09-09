param([string]$Folder='D:\DLSSNR-Lab\native-projection-256')
$ErrorActionPreference='Stop'
$env:DLSS5_VIT_CUBIN=Join-Path $Folder 'dlssnr-05.cubin'
foreach($Trial in 1..2){
 & (Join-Path $Folder 'native-contract.exe') (Join-Path $Folder 'attention.fp8') (Join-Path $Folder 'residual.fp8') (Join-Path $Folder 'weights.bin') (Join-Path $Folder "projection-$Trial.fp8") 256 16 projection
 if($LASTEXITCODE -ne 0){throw "projection failed: $LASTEXITCODE"}
}
$A=(Get-FileHash (Join-Path $Folder 'projection-1.fp8')).Hash
$B=(Get-FileHash (Join-Path $Folder 'projection-2.fp8')).Hash
Write-Output "first=$A second=$B"
if($A -ne $B){throw 'projection replay differs'}
