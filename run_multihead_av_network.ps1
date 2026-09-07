param([Parameter(Mandatory=$true)][string]$Folder,[switch]$SharedProb)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
$FourWaves=if($env:DLSS5_TEST_MULTIHEAD_FOUR_WAVES -eq '1'){1}else{0}
$ParallelSoftmax=if($env:DLSS5_TEST_PARALLEL_MULTIHEAD_SOFTMAX -eq '1'){1}else{0}
$EightWaves=if($env:DLSS5_TEST_MULTIHEAD_EIGHT_WAVES -eq '1'){1}else{0}
foreach($Channels in 64,128,256){
 $Name=if($Channels -eq 256){'native_wave_av.cso'}else{"native_wave_av_c$Channels.cso"}
 & $Dxc -I $Inc -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D "CHANNELS=$Channels" -D NATIVE_PRECOMPUTED_QKV=1 -D NATIVE_WAVE_SCORES=1 -D "NATIVE_PARALLEL_MULTIHEAD_SOFTMAX=$ParallelSoftmax" -D "NATIVE_MULTIHEAD_EIGHT_WAVES=$EightWaves" -D NATIVE_WAVE_AV=1 -D "NATIVE_MULTIHEAD_FOUR_WAVES=$FourWaves" -D "NATIVE_SHARED_PROB=$([int]$SharedProb.IsPresent)" native_c64.hlsl -Fo $Name
 if($LASTEXITCODE -ne 0){throw 'Multihead AV compilation failed'}
}
$env:DLSS5_TEST_WAVE_MULTIHEAD_AV='1'
& "$Folder\run_parallel_split_network.ps1" -Folder $Folder
exit $LASTEXITCODE
