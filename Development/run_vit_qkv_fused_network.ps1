param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH: ViT QKV projection + per-head normalize + E4M3 store in one kernel; the FP8 attention reads it directly
# (drops the scalar normalize dispatch and the pack8 dispatch per layer).
& $Dxc -I $Inc -T cs_6_10 -E project_fused -HV 2021 -enable-16bit-types -O3 -D NATIVE_FAST_ACCUMULATE=1 -D NATIVE_PACKED_INPUT=1 -D BLOCK_M=4 -D NATIVE_QKV_FUSED=1 -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_qkv.hlsl -Fo native_wave_vit_qkv_fused_m4.cso
if($LASTEXITCODE -ne 0){throw 'ViT fused QKV compilation failed'}
$env:DLSS5_VIT_QKV_FUSED='1'
& "$Folder\run_batch_submits2_network.ps1" -Folder $Folder
exit $LASTEXITCODE
