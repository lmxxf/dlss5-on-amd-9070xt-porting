param([Parameter(Mandatory=$true)][string]$Folder,[int]$BlockN=4)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# The fast chain's ViT build flags are set by runners further down the chain; set them here so the split-K variants match the packed weights.
foreach($Name in 'DLSS5_FAST_ACCUMULATE','DLSS5_FAST_EPILOGUE','DLSS5_FP8_OPERANDS','DLSS5_FP8_HIDDEN','DLSS5_FP8_LDS'){Set-Item -Path "Env:$Name" -Value '1'}
# FAST PATH: ViT contract / projection reduce with the four K partitions as parallel groups plus a combine pass.
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_SPLIT_K=$(if($env:DLSS5_VIT_SPLIT_PROBE){2}else{1})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_splitk.cso
if($LASTEXITCODE -ne 0){throw 'split-K reduce 4096 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E combine -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_SPLIT_K=$(if($env:DLSS5_VIT_SPLIT_PROBE){2}else{1})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_combine.cso
if($LASTEXITCODE -ne 0){throw 'split-K combine 4096 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_SPLIT_K=$(if($env:DLSS5_VIT_SPLIT_PROBE){2}else{1})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_splitk_1024.cso
if($LASTEXITCODE -ne 0){throw 'split-K reduce 1024 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E combine -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_SPLIT_K=$(if($env:DLSS5_VIT_SPLIT_PROBE){2}else{1})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_combine_1024.cso
if($LASTEXITCODE -ne 0){throw 'split-K combine 1024 compilation failed'}
$env:DLSS5_VIT_SPLIT_K='1'
& "$Folder\run_fast_vit_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
