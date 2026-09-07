param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D RAW_OUTPUT=1 -D NATIVE_WAVE_C32_SCORES=1 -D NATIVE_WAVE_C32_QKV=1 -D NATIVE_WAVE_C32_AV=1 -D NATIVE_WAVE_C32_PROJECTION=1 -D NATIVE_PAD_WAVE_C32_SCORES=1 preblock_attention_core.hlsl -Fo native_wave_c32_full_attention.cso
if($LASTEXITCODE -ne 0){throw 'Padded C32 scores compile failed'}
& "$Folder\run_multihead_av_network.ps1" -Folder $Folder
exit $LASTEXITCODE
