param([Parameter(Mandatory=$true)][string]$Folder,[int]$BlockN=4)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# Register-blocked wave FFN with f16 hidden storage for C64/C128/C256 Swin blocks.
foreach($Channels in 64,128,256){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 if($env:DLSS5_FP8_OPERANDS -eq '1'){
  foreach($Mapped in 0,1){
   $PackName=if($Mapped){"native_matrix_pack_fp8_mapped$Suffix.cso"}else{"native_matrix_pack_fp8$Suffix.cso"}
   & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D PACKED_INPUT=1 -D NATIVE_FP8_OPERANDS=1 -D "MAPPED_INPUT=$Mapped" native_matrix_pack.hlsl -Fo $PackName
   if($LASTEXITCODE -ne 0){throw "FP8 pack C$Channels mapped=$Mapped compilation failed"}
  }
 }
 foreach($Entry in 'expand','contract'){
  & $Dxc -I $Inc -T cs_6_10 -E $Entry -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_OPERANDS -eq '1'){1}else{0})" native_wave_ffn_blocked.hlsl -Fo "native_wave_ffn_${Entry}_blocked$Suffix.cso"
  if($LASTEXITCODE -ne 0){throw "Blocked FFN $Entry C$Channels compilation failed"}
 }
}
$env:DLSS5_TEST_BLOCKED_FFN='1'
& "$Folder\run_async_submit_network.ps1" -Folder $Folder
exit $LASTEXITCODE
