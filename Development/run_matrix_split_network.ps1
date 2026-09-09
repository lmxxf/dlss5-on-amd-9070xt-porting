param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
foreach($Name in 'pack','qkv'){
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=512 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" "native_matrix_$Name.hlsl" -Fo "native_matrix_${Name}_c512.cso"
 if($LASTEXITCODE -ne 0){throw "Compile failed: $Name"}
}
& $Dxc -I $Inc -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D CHANNELS=512 -D NATIVE_PRECOMPUTED_QKV=1 -D NATIVE_WAVE_SCORES=1 -D "NATIVE_HW_H=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})" -D "NATIVE_FAST_FP8=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})" native_c64.hlsl -Fo native_wave_split_attention.cso
if($LASTEXITCODE -ne 0){throw 'Split attention compile failed'}
foreach($Name in 'WAVE_C32_SCORES','WAVE_C32_QKV','WAVE_VIT_EXPAND','RESIDENT_WAVE_VIT_EXPAND','WAVE_VIT_REDUCE','MATRIX_SPLIT_ATTENTION'){
 [Environment]::SetEnvironmentVariable("DLSS5_TEST_$Name",'1','Process')
}
& "$Folder\run_wave_c128_network.ps1" -Folder $Folder -IncludeC64
exit $LASTEXITCODE
