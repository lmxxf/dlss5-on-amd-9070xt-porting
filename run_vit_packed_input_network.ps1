param([Parameter(Mandatory=$true)][string]$Folder,[int]$BlockN=4)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
foreach($Name in 'DLSS5_FAST_ACCUMULATE','DLSS5_FAST_EPILOGUE','DLSS5_FP8_OPERANDS','DLSS5_FP8_HIDDEN','DLSS5_FP8_LDS'){Set-Item -Path "Env:$Name" -Value '1'}
# FAST PATH: ViT operand copies (pack8/pack16) and packed-input kernels (expand, qkv, projection reduce incl. split-K).
& $Dxc -I $Inc -T cs_6_10 -E pack8 -HV 2021 -enable-16bit-types -O3 native_vit_pack.hlsl -Fo native_vit_pack8.cso
if($LASTEXITCODE -ne 0){throw 'pack8 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E pack16 -HV 2021 -enable-16bit-types -O3 native_vit_pack.hlsl -Fo native_vit_pack16.cso
if($LASTEXITCODE -ne 0){throw 'pack16 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E expand -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=1 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 native_wave_vit_blocked.hlsl -Fo native_wave_vit_expand_packed.cso
if($LASTEXITCODE -ne 0){throw 'packed expand compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_packed_1024.cso
if($LASTEXITCODE -ne 0){throw 'packed reduce 1024 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 -D NATIVE_SPLIT_K=1 native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_splitk_packed_1024.cso
if($LASTEXITCODE -ne 0){throw 'packed split-K reduce 1024 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E combine -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 -D NATIVE_SPLIT_K=1 native_wave_vit_blocked.hlsl -Fo native_wave_vit_combine_packed_1024.cso
if($LASTEXITCODE -ne 0){throw 'packed combine 1024 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E project -HV 2021 -enable-16bit-types -O3 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 native_wave_vit_qkv.hlsl -Fo native_wave_vit_qkv_packed.cso
if($LASTEXITCODE -ne 0){throw 'packed qkv compilation failed'}
$env:DLSS5_VIT_PACKED_INPUT='1'
& "$Folder\run_split_ffwd_waves4_network.ps1" -Folder $Folder
exit $LASTEXITCODE
