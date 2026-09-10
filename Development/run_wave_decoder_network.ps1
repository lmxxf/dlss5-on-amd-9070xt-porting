param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# Wave-matrix decoder entry (1024->512) and the four 2x upsample projections.
foreach($Pair in @(@(1024,512),@(512,256),@(256,128),@(128,64),@(64,32))){
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "INPUT_CHANNELS=$($Pair[0])" -D "OUTPUT_CHANNELS=$($Pair[1])" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_DECODER_TILED=$(if($env:DLSS5_BUILD_DECODER_TILED -eq '1' -and $Pair[0] -eq 1024){1}else{0})" native_wave_decoder_entry.hlsl -Fo "native_wave_decoder_$($Pair[0])_$($Pair[1]).cso"
 if($LASTEXITCODE -ne 0){throw "Wave decoder $($Pair[0])->$($Pair[1]) compilation failed"}
}
# FAST PATH (DLSS5_BUILD_C32_SKIP8): block 66 projection variant reading block 4's E4M3 main8 as the skip residual.
if($env:DLSS5_BUILD_C32_SKIP8 -eq '1'){
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "INPUT_CHANNELS=64" -D "OUTPUT_CHANNELS=32" -D "SKIP8=1" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" native_wave_decoder_entry.hlsl -Fo native_wave_decoder_64_32_skip8.cso
 if($LASTEXITCODE -ne 0){throw "Wave decoder 64->32 skip8 compilation failed"}
}
$env:DLSS5_TEST_WAVE_DECODER_LINEAR='1'
& "$Folder\run_local_c32_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
