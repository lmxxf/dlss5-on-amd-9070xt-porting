param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH step 4 probe: fused FFN quantizes through the hardware E4M3 cast (no scalar Ffast/H in the epilogues).
# Overrides the fp8act fused FFN shaders in this folder; keep it in its own release directory.
foreach($Channels in 256,128,64){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D NATIVE_FP8_OUTPUT=1 -D NATIVE_HW_QUANTIZE=1 native_wave_ffn_fused.hlsl -Fo "native_wave_ffn_fused_fp8act$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "HW-quantize fused FFN C$Channels compilation failed"}
}
& "$Folder\run_fp8_activations_network.ps1" -Folder $Folder
exit $LASTEXITCODE
