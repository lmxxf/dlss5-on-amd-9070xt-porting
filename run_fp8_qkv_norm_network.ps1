param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH step 3c: E4M3 Q/K/V between the fused QKV+normalize kernel and the direct attention kernel.
foreach($Channels in 256,128,64){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D NATIVE_FP8_QKV_OUT=1 -D "NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_TILED_PQ -eq '1'){1}else{0})" native_wave_qkv_normalize.hlsl -Fo "native_wave_qkv_normalize_fp8qkv$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "FP8 QKV normalize C$Channels compilation failed"}
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D "CHANNELS=$Channels" -D DIRECT_NORMALIZE=0 -D NATIVE_FAST_ACCUMULATE=1 -D "NATIVE_HW_H=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})" -D NATIVE_FAST_ATTENTION=1 -D NATIVE_FP8_OUTPUT=1 -D NATIVE_FP8_QKV=1 -D "NATIVE_ATTN_FAST2=$(if($env:DLSS5_BUILD_ATTN_FAST2 -eq '1'){1}else{0})" native_wave_attention_direct.hlsl -Fo "native_wave_attention_direct_fp8qkv_fp8act$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "FP8 QKV attention C$Channels compilation failed"}
}
$env:DLSS5_FP8_QKV_NORM='1'
& "$Folder\run_hw_quantize_network.ps1" -Folder $Folder
exit $LASTEXITCODE
