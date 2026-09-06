param([string]$Folder='D:\DLSSNR-Lab\native-block256')
$ErrorActionPreference='Stop'
$env:DLSS5_VIT_CUBIN=Join-Path $Folder 'dlssnr-05.cubin'
function Check { if($LASTEXITCODE -ne 0){throw "Original kernel failed: $LASTEXITCODE"} }
foreach($Trial in 1..2){
 $P=Join-Path $Folder "trial-$Trial"
 & "$Folder\native-expand.exe" "$Folder\input.fp8" "$P-expand.fp8" "$Folder\expand.weights" 256 64; Check
 & "$Folder\native-contract.exe" "$P-expand.fp8" "$Folder\input.fp8" "$Folder\contract.weights" "$P-contract.fp8" 256 16; Check
 & "$Folder\native-qkv-extent.exe" "$P-contract.fp8" "$Folder\qkv.weights" "$P-qkv" 16 16 32; Check
 & "$Folder\native-vit-attention-oracle.exe" "$P-qkv-0.fp8" "$P-qkv-1.fp8" "$P-qkv-2.fp8" "$P-attention.fp8" 16 16 32; Check
 & "$Folder\native-contract.exe" "$P-attention.fp8" "$P-contract.fp8" "$Folder\projection.weights" "$P-projection.fp8" 256 16 projection; Check
}
foreach($Stage in 'expand','contract','qkv-0','qkv-1','qkv-2','attention','projection'){
 $A=(Get-FileHash "$Folder\trial-1-$Stage.fp8").Hash
 $B=(Get-FileHash "$Folder\trial-2-$Stage.fp8").Hash
 Write-Output "$Stage first=$A second=$B"
 if($A -ne $B){throw "Replay mismatch: $Stage"}
}
