$ErrorActionPreference='Stop'
$Folder='D:\DLSSNR-Lab\native-network70-matrix-c128'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
foreach($Name in 'pack','qkv','expand'){
 $Output=if($Name -eq 'expand'){'native_matrix_expand_packed_c128.cso'}else{"native_matrix_${Name}_c128.cso"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=128 -D PACKED_INPUT=1 "native_matrix_$Name.hlsl" -Fo $Output
 if($LASTEXITCODE -ne 0){throw "Compile failed: $Name"}
}
foreach($Name in 'TILED_C64','SPLIT_PROJECTION','SPLIT_FFWD','TILED_QKV','SHARED_C32','RESIDENT_NOISE','CACHE_C32_INPUT','PAD_C32_LDS','PAD_MULTIHEAD_LDS','MATRIX_C256','MATRIX_C128'){
 [Environment]::SetEnvironmentVariable("DLSS5_TEST_$Name",'1','Process')
}
& "$Folder\run_native_temporal_network70.ps1" -Folder $Folder -PostShift 3 -GpuProfile
exit $LASTEXITCODE
