param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH: wave-matrix preblock prefix mix (16 features -> 32 channels via two MMAs per 16 pixels).
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 native_wave_prefix.hlsl -Fo native_wave_prefix.cso
if($LASTEXITCODE -ne 0){throw 'wave prefix compilation failed'}
$env:DLSS5_WAVE_PREFIX='1'
& "$Folder\run_vit_attn_fp8_network.ps1" -Folder $Folder
exit $LASTEXITCODE
