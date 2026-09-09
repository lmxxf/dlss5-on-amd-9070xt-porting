param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH step 2b: Swin FFN expand+contract fused, hidden block in LDS (multihead C64/C128/C256).
foreach($Channels in 256,128,64){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" native_wave_ffn_fused.hlsl -Fo "native_wave_ffn_fused$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Fused FFN C$Channels compilation failed"}
}
$env:DLSS5_FUSED_FFN='1'
& "$Folder\run_fused_qkv_normalize_network.ps1" -Folder $Folder
exit $LASTEXITCODE
