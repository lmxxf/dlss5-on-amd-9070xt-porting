param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "NATIVE_C32_HALF_STREAM=$(if($env:DLSS5_BUILD_C32_HALF_STREAM -eq '1'){1}else{0})" -D "NATIVE_FAST_F=$(if($env:DLSS5_BUILD_FAST_F -eq '1'){1}else{0})" -D "NATIVE_STATIC_LENGTH=$(if($env:DLSS5_BUILD_STATIC_LENGTH -eq '1'){1}else{0})" -D "NATIVE_RAW_OUTPUT_STORE=$(if($env:DLSS5_TEST_C32_FFN_RAW_STORE -eq '1'){1}else{0})" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_C32_FFN_FAST2=$(if($env:DLSS5_BUILD_C32_FFN_FAST2 -eq '1'){1}else{0})" -D "NATIVE_C32_FFN_FP8=$(if($env:DLSS5_BUILD_C32_FFN_FP8 -eq '1'){1}else{0})" -D "NATIVE_C32_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_C32_TILED_WEIGHTS -eq '1'){1}else{0})" -D "NATIVE_C32_FFN_FAST3=$(if($env:DLSS5_BUILD_C32_FFN_FAST3 -eq '1'){1}else{0})" -D "NATIVE_C32_MAPPED_INPUT=$(if($env:DLSS5_BUILD_C32_MAPPED_INPUT -eq '1'){1}else{0})" -D "NATIVE_C32_PRECISE_CHAIN=$(if($env:DLSS5_BUILD_C32_PRECISE_CHAIN -eq '1'){1}else{0})" native_wave_c32_ffn_blocked.hlsl -Fo native_wave_c32_ffn_blocked.cso
if($LASTEXITCODE -ne 0){throw 'Blocked C32 FFN compilation failed'}
$env:DLSS5_TEST_BLOCKED_C32_FFN='1'
& "$Folder\run_split_ffwd_blocked_network.ps1" -Folder $Folder
exit $LASTEXITCODE
