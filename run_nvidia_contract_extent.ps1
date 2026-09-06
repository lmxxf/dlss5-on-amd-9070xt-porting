param([string]$Folder='D:\DLSSNR-Lab\native-contract-256')
$ErrorActionPreference='Stop'
$env:DLSS5_VIT_CUBIN=Join-Path $Folder 'dlssnr-05.cubin'
foreach($Trial in 1..2){
 & (Join-Path $Folder 'native-contract.exe') (Join-Path $Folder 'expand.fp8') (Join-Path $Folder 'input.fp8') (Join-Path $Folder 'weights.bin') (Join-Path $Folder "contract-$Trial.fp8") 256 16
 if($LASTEXITCODE -ne 0){throw "contract failed: $LASTEXITCODE"}
}
$A=(Get-FileHash (Join-Path $Folder 'contract-1.fp8')).Hash
$B=(Get-FileHash (Join-Path $Folder 'contract-2.fp8')).Hash
Write-Output "first=$A second=$B"
if($A -ne $B){throw 'contract replay differs'}
