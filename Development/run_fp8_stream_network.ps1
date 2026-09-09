param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH: E4M3 residual stream in the multihead blocks (result[0] and chained raster outputs as bytes; no QKV pack; byte-gather shift pack).
$Common=@('-I',$Inc,'-T','cs_6_10','-E','main','-HV','2021','-enable-16bit-types','-O3','-D','NATIVE_FAST_ACCUMULATE=1','-D','NATIVE_HW_H=1','-D','NATIVE_FP8_OPERANDS=1','-D','NATIVE_FP8_INPUT=1','-D',"NATIVE_DIRECT_CAST=$(if($env:DLSS5_BUILD_DIRECT_CAST -eq '1'){1}else{0})",'-D',"NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_TILED_PQ -eq '1'){1}else{0})")
foreach($Channels in 256,128,64){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D PACKED_INPUT=1 -D NATIVE_FP8_OPERANDS=1 -D MAPPED_INPUT=1 -D NATIVE_FP8_SOURCE=1 native_matrix_pack.hlsl -Fo "native_matrix_pack_fp8_mapped_src8$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "FP8-source mapped pack C$Channels compilation failed"}
 foreach($Variant in @(
   @('native_wave_project_mapfeature_fp8act_f8out',@('-D','MAP_FEATURE=1','-D','NATIVE_FP8_STORE=1')),
   @('native_wave_project_mapfeature_fp8act_f8in_f8out',@('-D','MAP_FEATURE=1','-D','NATIVE_FP8_FEATURE=1','-D','NATIVE_FP8_STORE=1')),
   @('native_wave_project_mapoutput_fp8act_f8in_f8out',@('-D','RAW=0','-D','MAP_OUTPUT=1','-D','NATIVE_FP8_FEATURE=1','-D','NATIVE_FP8_STORE=1')),
   @('native_wave_project_mapoutput_fp8act_f8in',@('-D','RAW=0','-D','MAP_OUTPUT=1','-D','NATIVE_FP8_FEATURE=1')),
   @('native_wave_project_raw_mapoutput_fp8act_f8in',@('-D','RAW=1','-D','MAP_OUTPUT=1','-D','NATIVE_FP8_FEATURE=1')))){
  $Extra=$Variant[1]
  if($env:DLSS5_BUILD_MATRIX_RESIDUAL -eq '1' -and $Variant[0] -notlike '*mapfeature*'){$Extra=$Extra+@('-D','NATIVE_MATRIX_RESIDUAL=1')}
  & $Dxc @Common -D "MATRIX_CHANNELS=$Channels" @Extra native_wave_project.hlsl -Fo "$($Variant[0])$Suffix.cso"
  if($LASTEXITCODE -ne 0){throw "Projection $($Variant[0]) C$Channels compilation failed"}
 }
}
$env:DLSS5_FP8_STREAM='1'
& "$Folder\run_c32_mapped_input_network.ps1" -Folder $Folder
exit $LASTEXITCODE
