param([Parameter(Mandatory=$true)][string]$Folder,[int]$BlockN=4)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# Register-blocked wave FFN with f16 hidden storage for C64/C128/C256 Swin blocks.
foreach($Channels in 64,128,256){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 foreach($Entry in 'expand','contract'){
  & $Dxc -I $Inc -T cs_6_10 -E $Entry -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" native_wave_ffn_blocked.hlsl -Fo "native_wave_ffn_${Entry}_blocked$Suffix.cso"
  if($LASTEXITCODE -ne 0){throw "Blocked FFN $Entry C$Channels compilation failed"}
 }
}
$env:DLSS5_TEST_BLOCKED_FFN='1'
& "$Folder\run_async_submit_network.ps1" -Folder $Folder
exit $LASTEXITCODE
