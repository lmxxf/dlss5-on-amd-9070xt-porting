param([string]$Folder='D:\DLSSNR-Lab\native-network70-matrix-c128',[switch]$IncludeC64)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
foreach($Channels in $(if($IncludeC64){128,64}else{128})){foreach($Name in 'pack','qkv','expand'){
 $Output=if($Name -eq 'expand'){"native_matrix_expand_packed_c$Channels.cso"}else{"native_matrix_${Name}_c$Channels.cso"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D PACKED_INPUT=1 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" "native_matrix_$Name.hlsl" -Fo $Output
 if($LASTEXITCODE -ne 0){throw "Compile failed: $Name"}
}}
[Environment]::SetEnvironmentVariable('DLSS5_TEST_MATRIX_C64',$(if($IncludeC64){'1'}else{'0'}),'Process')
foreach($Name in 'TILED_C64','SPLIT_PROJECTION','SPLIT_FFWD','TILED_QKV','SHARED_C32','RESIDENT_NOISE','CACHE_C32_INPUT','PAD_C32_LDS','PAD_MULTIHEAD_LDS','MATRIX_C256','MATRIX_C128'){
 [Environment]::SetEnvironmentVariable("DLSS5_TEST_$Name",'1','Process')
}
& "$Folder\run_native_temporal_network70.ps1" -Folder $Folder -PostShift 3 -GpuProfile
exit $LASTEXITCODE
