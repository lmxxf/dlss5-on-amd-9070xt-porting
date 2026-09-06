param([string]$Folder='D:\DLSSNR-Lab\native-qkv-extent',[ValidateSet(64,128,256)][int]$Tokens=256)
$ErrorActionPreference='Stop'
$env:DLSS5_VIT_CUBIN=Join-Path $Folder 'dlssnr-05.cubin'
$Width=if($Tokens -eq 64){8}else{16}
$Height=$Tokens/$Width
$GridX=[int]([Math]::Ceiling($Tokens/128.0)*16)
foreach($Trial in 1..2){
 & (Join-Path $Folder 'native-qkv-extent.exe') (Join-Path $Folder 'input.fp8') (Join-Path $Folder 'weights.bin') (Join-Path $Folder "qkv-$Trial") $Width $Height $GridX
 if($LASTEXITCODE -ne 0){throw "QKV probe failed: $LASTEXITCODE"}
}
foreach($Part in 0..2){
 $A=(Get-FileHash (Join-Path $Folder "qkv-1-$Part.fp8")).Hash
 $B=(Get-FileHash (Join-Path $Folder "qkv-2-$Part.fp8")).Hash
 Write-Output "part=$Part first=$A second=$B"
 if($A -ne $B){throw 'QKV replay differs'}
}
