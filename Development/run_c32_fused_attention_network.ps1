param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH: C32 attention as two dispatches (qkv+normalize, attention+projection).
foreach($Pass in @(@(0,'qkv'),@(2,'attention'))){
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E $Pass[1] -HV 2021 -enable-16bit-types -O3 -D "NATIVE_C32_EPILOGUE=$(if($env:DLSS5_BUILD_C32_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_C32_HALF_STREAM=$(if($env:DLSS5_BUILD_C32_HALF_STREAM -eq '1'){1}else{0})" -D "PASS=$($Pass[0])" -D RAW_OUTPUT=1 -D NATIVE_FAST_ACCUMULATE=1 -D "NATIVE_HW_H=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})" -D NATIVE_FAST_ATTENTION=1 -D NATIVE_C32_FUSED=1 -D "NATIVE_C32_FP8_QKV=$(if($env:DLSS5_BUILD_C32_FP8_QKV -eq '1'){1}else{0})" -D "NATIVE_C32_ATTN_FAST2=$(if($env:DLSS5_BUILD_C32_ATTN_FAST2 -eq '1'){1}else{0})" -D "NATIVE_C32_ATTN_FAST3=$(if($env:DLSS5_BUILD_C32_ATTN_FAST3 -eq '1'){1}else{0})" -D "NATIVE_C32_ATTN_FAST4=$(if($env:DLSS5_BUILD_C32_ATTN_FAST4 -eq '1'){1}else{0})" native_wave_c32_split_attention.hlsl -Fo "native_wave_c32_fused_$($Pass[1]).cso"
 if($LASTEXITCODE -ne 0){throw "Fused C32 attention $($Pass[1]) compilation failed"}
}
if($env:DLSS5_BUILD_C32_WAVE_ATTENTION -eq '1'){
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention_wave -HV 2021 -enable-16bit-types -O3 -D "NATIVE_C32_EPILOGUE=$(if($env:DLSS5_BUILD_C32_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_C32_HALF_STREAM=$(if($env:DLSS5_BUILD_C32_HALF_STREAM -eq '1'){1}else{0})" -D PASS=4 -D RAW_OUTPUT=1 -D NATIVE_FAST_ACCUMULATE=1 -D NATIVE_HW_H=1 -D NATIVE_FAST_ATTENTION=1 -D NATIVE_C32_FUSED=1 -D NATIVE_C32_FP8_QKV=1 native_wave_c32_split_attention.hlsl -Fo native_wave_c32_fused_attention_wave.cso
 if($LASTEXITCODE -ne 0){throw 'C32 wave attention compilation failed'}
 $env:DLSS5_C32_WAVE_ATTENTION='1'
}
$env:DLSS5_C32_FUSED_ATTENTION='1'
& "$Folder\run_fast_prefix_network.ps1" -Folder $Folder
exit $LASTEXITCODE
