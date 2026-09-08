param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# Shift pack/crop folded into the multihead body (raster-mapped pack, residual and output).
foreach($Channels in 64,128,256){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D PACKED_INPUT=1 -D MAPPED_INPUT=1 native_matrix_pack.hlsl -Fo "native_matrix_pack_mapped$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Mapped pack C$Channels compilation failed"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D MAP_FEATURE=1 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" native_wave_project.hlsl -Fo "native_wave_project_mapfeature$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Mapped-feature projection C$Channels compilation failed"}
 foreach($Raw in 0,1){
  $Name=if($Raw){"native_wave_project_raw_mapoutput$Suffix.cso"}else{"native_wave_project_mapoutput$Suffix.cso"}
  & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D "RAW=$Raw" -D MAP_OUTPUT=1 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" native_wave_project.hlsl -Fo $Name
  if($LASTEXITCODE -ne 0){throw "Mapped-output projection C$Channels raw=$Raw compilation failed"}
 }
}
$env:DLSS5_TEST_FUSED_SHIFT='1'
& "$Folder\run_split_c32_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
