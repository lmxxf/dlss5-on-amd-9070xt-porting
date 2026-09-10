param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
& $Dxc -I $Inc -T cs_6_10 -E project -HV 2021 -enable-16bit-types -O3 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_qkv.hlsl -Fo native_wave_vit_qkv.cso
if($LASTEXITCODE -ne 0){throw 'Wave ViT QKV compilation failed'}
$env:DLSS5_TEST_WAVE_VIT_QKV='1'
& "$Folder\run_coalesced_shift_stack.ps1" -Folder $Folder
exit $LASTEXITCODE
