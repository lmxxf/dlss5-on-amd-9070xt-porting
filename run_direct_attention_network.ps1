param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# Multihead attention with normalized window-major f16 Q/K/V read by wave loads.
foreach($Channels in 64,128,256){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E normalize -HV 2021 -enable-16bit-types -O3 -D "CHANNELS=$Channels" -D DIRECT_NORMALIZE=1 native_wave_attention_direct.hlsl -Fo "native_wave_attention_normalize$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Direct attention normalize C$Channels compilation failed"}
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D "CHANNELS=$Channels" -D DIRECT_NORMALIZE=0 native_wave_attention_direct.hlsl -Fo "native_wave_attention_direct$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Direct attention C$Channels compilation failed"}
}
$env:DLSS5_TEST_DIRECT_ATTENTION='1'
& "$Folder\run_wave_split_project_network.ps1" -Folder $Folder
exit $LASTEXITCODE
