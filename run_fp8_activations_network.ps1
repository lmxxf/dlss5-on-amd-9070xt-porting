param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH step 3: E4M3 activations between fused FFN contract / attention and their projections.
# Producers store E4M3 bytes (NATIVE_FP8_OUTPUT), the five projection variants load A tiles directly (NATIVE_FP8_INPUT).
$Common=@('-I',$Inc,'-T','cs_6_10','-E','main','-HV','2021','-enable-16bit-types','-O3','-D','NATIVE_FAST_ACCUMULATE=1','-D',"NATIVE_HW_H=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})",'-D','NATIVE_FP8_OPERANDS=1')
foreach($Channels in 256,128,64){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc @Common -D "MATRIX_CHANNELS=$Channels" -D NATIVE_FP8_OUTPUT=1 -D "NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_TILED_WEIGHTS -eq '1'){1}else{0})" native_wave_ffn_fused.hlsl -Fo "native_wave_ffn_fused_fp8act$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Fused FFN fp8act C$Channels compilation failed"}
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D "CHANNELS=$Channels" -D DIRECT_NORMALIZE=0 -D NATIVE_FAST_ACCUMULATE=1 -D "NATIVE_HW_H=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})" -D NATIVE_FAST_ATTENTION=1 -D NATIVE_FP8_OUTPUT=1 native_wave_attention_direct.hlsl -Fo "native_wave_attention_direct_fp8act$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Attention fp8act C$Channels compilation failed"}
 foreach($Variant in @(@('native_wave_project',@('-D','RAW=0')),@('native_wave_project_raw',@('-D','RAW=1')),@('native_wave_project_mapfeature',@('-D','MAP_FEATURE=1')),@('native_wave_project_mapoutput',@('-D','RAW=0','-D','MAP_OUTPUT=1')),@('native_wave_project_raw_mapoutput',@('-D','RAW=1','-D','MAP_OUTPUT=1')))){
  $Extra=$Variant[1]
  & $Dxc @Common -D "MATRIX_CHANNELS=$Channels" -D NATIVE_FP8_INPUT=1 @Extra native_wave_project.hlsl -Fo "$($Variant[0])_fp8act$Suffix.cso"
  if($LASTEXITCODE -ne 0){throw "Projection $($Variant[0]) fp8act C$Channels compilation failed"}
 }
}
$env:DLSS5_FP8_ACTIVATIONS='1'
& "$Folder\run_fused_ffn_network.ps1" -Folder $Folder
exit $LASTEXITCODE
