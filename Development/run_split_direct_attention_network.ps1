param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH: C512 attention through the direct path (FP8 pack, fused QKV+normalize, FP8 Q/K/V attention with FP8 output, FP8-input projection).
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=512 -D PACKED_INPUT=1 -D NATIVE_FP8_OPERANDS=1 -D MAPPED_INPUT=0 native_matrix_pack.hlsl -Fo native_matrix_pack_fp8_c512.cso
if($LASTEXITCODE -ne 0){throw 'FP8 pack C512 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=512 -D NATIVE_FP8_QKV_OUT=1 -D "NATIVE_QKV_FAST2=$(if($env:DLSS5_BUILD_QKV_FAST2 -eq '1'){1}else{0})" -D "NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_SPLIT_TILED -eq '1'){1}else{0})" native_wave_qkv_normalize.hlsl -Fo native_wave_qkv_normalize_fp8qkv_c512.cso
if($LASTEXITCODE -ne 0){throw 'fused QKV C512 compilation failed'}
& $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D CHANNELS=512 -D DIRECT_NORMALIZE=0 -D NATIVE_FAST_ACCUMULATE=1 -D NATIVE_HW_H=1 -D NATIVE_FAST_ATTENTION=1 -D NATIVE_FP8_OUTPUT=1 -D NATIVE_FP8_QKV=1 -D "NATIVE_ATTN_FAST2=$(if($env:DLSS5_BUILD_ATTN_FAST2 -eq '1'){1}else{0})" native_wave_attention_direct.hlsl -Fo native_wave_attention_direct_fp8qkv_fp8act_c512.cso
if($LASTEXITCODE -ne 0){throw 'direct attention C512 compilation failed'}
foreach($Raw in 0,1){
 $Name=if($Raw){'native_wave_project_raw_fp8act_c512.cso'}else{'native_wave_project_fp8act_c512.cso'}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=512 -D "RAW=$Raw" -D NATIVE_FAST_ACCUMULATE=1 -D NATIVE_HW_H=1 -D NATIVE_FP8_OPERANDS=1 -D NATIVE_FP8_INPUT=1 -D "NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_SPLIT_TILED -eq '1'){1}else{0})" native_wave_project.hlsl -Fo $Name
 if($LASTEXITCODE -ne 0){throw "FP8-input projection C512 raw=$Raw compilation failed"}
}
$env:DLSS5_SPLIT_DIRECT_ATTENTION='1'
& "$Folder\run_fp8_stream_network.ps1" -Folder $Folder
exit $LASTEXITCODE
