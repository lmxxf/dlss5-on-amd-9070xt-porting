param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 native_wave_vit_attention.hlsl -Fo native_wave_vit_attention.cso
if($LASTEXITCODE -ne 0){throw 'Wave ViT attention compilation failed'}
$env:DLSS5_TEST_WAVE_VIT_ATTENTION='1'
& "$Folder\run_coalesced_finish_network.ps1" -Folder $Folder
exit $LASTEXITCODE
