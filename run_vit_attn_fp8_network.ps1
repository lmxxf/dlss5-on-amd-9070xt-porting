param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH: ViT attention on E4M3 Q/K/V with register exp, hardware-cast P, MMA denominators, FP8 x FP8 PV.
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 native_wave_vit_attention_fp8.hlsl -Fo native_wave_vit_attention_fp8.cso
if($LASTEXITCODE -ne 0){throw 'ViT FP8 attention compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E pack8 -HV 2021 -enable-16bit-types -O3 native_wave_vit_attention_fp8.hlsl -Fo native_wave_vit_attention_pack8.cso
if($LASTEXITCODE -ne 0){throw 'ViT FP8 attention pack compilation failed'}
$env:DLSS5_VIT_ATTN_FP8='1'
& "$Folder\run_attn_fast2_network.ps1" -Folder $Folder
exit $LASTEXITCODE
