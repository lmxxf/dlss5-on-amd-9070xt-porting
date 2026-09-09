param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
foreach($Channels in 64,128,256){foreach($Name in 'native_head_pool','native_wave_head_project'){
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" "$Name.hlsl" -Fo "${Name}_c$Channels.cso"
 if($LASTEXITCODE -ne 0){throw "Compile failed: $Name $Channels"}
}}
foreach($Name in 'WAVE_C32_FFN','WAVE_C32_FFN_LOCAL','WAVE_C32_AV','WAVE_C32_PROJECTION','SPLIT_PREBLOCK_FFN','WAVE_HEAD','WAVE_DOWNSAMPLE'){
 [Environment]::SetEnvironmentVariable("DLSS5_TEST_$Name",'1','Process')
}
$env:DLSS5_TEST_WAVE_SPLIT_FFWD='0'
& "$Folder\run_matrix_split_network.ps1" -Folder $Folder
exit $LASTEXITCODE
