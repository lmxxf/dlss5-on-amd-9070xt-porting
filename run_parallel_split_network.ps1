param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "NATIVE_SPLIT_FFWD_BLOCKED=$(if($env:DLSS5_BUILD_SPLIT_FFWD_BLOCKED -eq '1'){1}else{0})" native_wave_split_ffwd_parallel.hlsl -Fo native_wave_split_ffwd_parallel.cso
if($LASTEXITCODE -ne 0){throw 'Parallel split FFWD compilation failed'}
$env:DLSS5_TEST_PARALLEL_SPLIT_FFWD='1'
& "$Folder\run_wave_vit_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
