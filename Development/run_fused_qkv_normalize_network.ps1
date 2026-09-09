param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH step 2a: QKV GEMM + normalize fused into one wave kernel (multihead C64/C128/C256).
foreach($Channels in 256,128,64){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D "NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_TILED_PQ -eq '1'){1}else{0})" native_wave_qkv_normalize.hlsl -Fo "native_wave_qkv_normalize$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Fused QKV normalize C$Channels compilation failed"}
}
$env:DLSS5_FUSED_QKV_NORMALIZE='1'
& "$Folder\run_fast_temporal_network.ps1" -Folder $Folder
exit $LASTEXITCODE
