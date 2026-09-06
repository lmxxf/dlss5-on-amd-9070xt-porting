param([string]$Folder='D:\DLSSNR-Lab\native-block256',[ValidateRange(31,38)][int]$LastBlock=31)
$ErrorActionPreference='Stop'
$env:DLSS5_VIT_CUBIN=Join-Path $Folder 'dlssnr-05.cubin'
function Check { if($LASTEXITCODE -ne 0){throw "Original kernel failed: $LASTEXITCODE"} }
foreach($Trial in 1..2){
 $Source=Join-Path $Folder 'input.fp8'
 foreach($Block in 31..$LastBlock){
 $Sub=if($LastBlock -eq 31){$Folder}else{Join-Path $Folder "block$Block"}
 if(!(Test-Path $Sub)){throw "Missing block directory: $Sub"}
 $P=Join-Path $Sub "trial-$Trial"
 & "$Folder\native-expand.exe" $Source "$P-expand.fp8" "$Sub\expand.weights" 256 64; Check
 & "$Folder\native-contract.exe" "$P-expand.fp8" $Source "$Sub\contract.weights" "$P-contract.fp8" 256 16; Check
 & "$Folder\native-qkv-extent.exe" "$P-contract.fp8" "$Sub\qkv.weights" "$P-qkv" 16 16 32; Check
 & "$Folder\native-vit-attention-oracle.exe" "$P-qkv-0.fp8" "$P-qkv-1.fp8" "$P-qkv-2.fp8" "$P-attention.fp8" 16 16 32; Check
 & "$Folder\native-contract.exe" "$P-attention.fp8" "$P-contract.fp8" "$Sub\projection.weights" "$P-projection.fp8" 256 16 projection; Check
 $Source="$P-projection.fp8"
 }
}
foreach($Block in 31..$LastBlock){
$Sub=if($LastBlock -eq 31){$Folder}else{Join-Path $Folder "block$Block"}
foreach($Stage in 'expand','contract','qkv-0','qkv-1','qkv-2','attention','projection'){
 $A=(Get-FileHash "$Sub\trial-1-$Stage.fp8").Hash
 $B=(Get-FileHash "$Sub\trial-2-$Stage.fp8").Hash
 Write-Output "block=$Block $Stage first=$A second=$B"
 if($A -ne $B){throw "Replay mismatch: $Stage"}
}
}
