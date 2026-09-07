param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# Wave-matrix FFWD/attention projections for the 512-channel split blocks.
foreach($Raw in 0,1){
 $Name=if($Raw){'native_wave_project_raw_c512.cso'}else{'native_wave_project_c512.cso'}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=512 -D "RAW=$Raw" native_wave_project.hlsl -Fo $Name
 if($LASTEXITCODE -ne 0){throw "Wave split projection raw=$Raw compilation failed"}
}
$env:DLSS5_TEST_WAVE_SPLIT_PROJECT='1'
& "$Folder\run_wave_decoder_network.ps1" -Folder $Folder
exit $LASTEXITCODE
