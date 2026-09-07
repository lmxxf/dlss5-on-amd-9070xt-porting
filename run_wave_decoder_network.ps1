param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# Wave-matrix decoder entry (1024->512) and the four 2x upsample projections.
foreach($Pair in @(@(1024,512),@(512,256),@(256,128),@(128,64),@(64,32))){
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "INPUT_CHANNELS=$($Pair[0])" -D "OUTPUT_CHANNELS=$($Pair[1])" native_wave_decoder_entry.hlsl -Fo "native_wave_decoder_$($Pair[0])_$($Pair[1]).cso"
 if($LASTEXITCODE -ne 0){throw "Wave decoder $($Pair[0])->$($Pair[1]) compilation failed"}
}
$env:DLSS5_TEST_WAVE_DECODER_LINEAR='1'
& "$Folder\run_local_c32_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
