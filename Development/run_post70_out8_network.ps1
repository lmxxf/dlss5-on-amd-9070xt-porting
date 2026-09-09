param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH: post70 attention stores an E4M3 copy of the block output; the fast rgb head reads bytes (283MB -> 71MB).
& $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D "NATIVE_C32_EPILOGUE=$(if($env:DLSS5_BUILD_C32_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_C32_HALF_STREAM=$(if($env:DLSS5_BUILD_C32_HALF_STREAM -eq '1'){1}else{0})" -D PASS=2 -D RAW_OUTPUT=1 -D NATIVE_FAST_ACCUMULATE=1 -D NATIVE_HW_H=1 -D NATIVE_FAST_ATTENTION=1 -D NATIVE_C32_FUSED=1 -D NATIVE_C32_FP8_QKV=1 -D NATIVE_C32_ATTN_FAST2=1 -D NATIVE_C32_ATTN_FAST3=1 -D NATIVE_C32_ATTN_FAST4=1 -D NATIVE_C32_OUT8=1 native_wave_c32_split_attention.hlsl -Fo native_wave_c32_fused_attention_out8.cso
if($LASTEXITCODE -ne 0){throw 'post70 out8 attention compilation failed'}
$env:DLSS5_POST70_OUT8='1'
& "$Folder\run_post70_fast_rgb_network.ps1" -Folder $Folder
exit $LASTEXITCODE
