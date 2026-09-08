param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH: wave-matrix C32 downsample projection (ds4) replacing the scalar cs_5_1 kernel.
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 native_wave_c32_ds.hlsl -Fo native_wave_c32_ds.cso
if($LASTEXITCODE -ne 0){throw 'wave C32 DS compilation failed'}
$env:DLSS5_WAVE_C32_DS='1'
& "$Folder\run_c32_chain_raw_network.ps1" -Folder $Folder
exit $LASTEXITCODE
