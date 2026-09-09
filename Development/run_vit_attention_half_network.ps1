param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "NATIVE_FAST_VIT_ATTENTION=$(if($env:DLSS5_BUILD_FAST_VIT_ATTENTION -eq '1'){1}else{0})" native_wave_vit_attention_half.hlsl -Fo native_wave_vit_attention_half.cso
if($LASTEXITCODE -ne 0){throw 'Half ViT attention compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E pack -HV 2021 -enable-16bit-types -O3 native_wave_vit_attention_half.hlsl -Fo native_wave_vit_attention_pack.cso
if($LASTEXITCODE -ne 0){throw 'ViT attention pack compilation failed'}
$env:DLSS5_TEST_WAVE_VIT_ATTENTION_HALF='1'
& "$Folder\run_fused_exp_network.ps1" -Folder $Folder
exit $LASTEXITCODE
