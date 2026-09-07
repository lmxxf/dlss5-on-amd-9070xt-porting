param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
foreach($Name in 'expand','contract'){
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=128 "native_wave_$Name.hlsl" -Fo "native_wave_${Name}_c128.cso"
 if($LASTEXITCODE -ne 0){throw "Wave compile failed: $Name"}
}
& $Dxc -I $Inc -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D CHANNELS=128 -D NATIVE_PRECOMPUTED_QKV=1 -D NATIVE_WAVE_SCORES=1 native_c64.hlsl -Fo native_wave_scores_c128.cso
if($LASTEXITCODE -ne 0){throw 'Wave scores compile failed'}
$env:DLSS5_TEST_WAVE_C256='1'
$env:DLSS5_TEST_WAVE_C128='1'
$env:DLSS5_TEST_SHARED_MATRIX_WORKSPACE='1'
$env:DLSS5_TEST_MEMORY_BUDGET='1'
$env:DLSS5_TEST_FRAME_COUNT='15'
& "$Folder\run_matrix_c128_network.ps1" -Folder $Folder
exit $LASTEXITCODE
