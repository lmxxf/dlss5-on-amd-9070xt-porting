param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# Wave-matrix FFN/attention output projections for C64/C128/C256 (raw variant for group-final blocks).
foreach($Channels in 64,128,256){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 foreach($Raw in 0,1){
  $Name=if($Raw){"native_wave_project_raw$Suffix.cso"}else{"native_wave_project$Suffix.cso"}
  & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D "RAW=$Raw" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" native_wave_project.hlsl -Fo $Name
  if($LASTEXITCODE -ne 0){throw "Wave projection C$Channels raw=$Raw compilation failed"}
 }
}
$env:DLSS5_TEST_WAVE_PROJECT='1'
& "$Folder\run_vit_attention_half_network.ps1" -Folder $Folder
exit $LASTEXITCODE
