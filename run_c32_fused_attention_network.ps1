param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH: C32 attention as two dispatches (qkv+normalize, attention+projection).
foreach($Pass in @(@(0,'qkv'),@(2,'attention'))){
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E $Pass[1] -HV 2021 -enable-16bit-types -O3 -D "PASS=$($Pass[0])" -D RAW_OUTPUT=1 -D NATIVE_FAST_ACCUMULATE=1 -D NATIVE_FAST_ATTENTION=1 -D NATIVE_C32_FUSED=1 native_wave_c32_split_attention.hlsl -Fo "native_wave_c32_fused_$($Pass[1]).cso"
 if($LASTEXITCODE -ne 0){throw "Fused C32 attention $($Pass[1]) compilation failed"}
}
$env:DLSS5_C32_FUSED_ATTENTION='1'
& "$Folder\run_fast_prefix_network.ps1" -Folder $Folder
exit $LASTEXITCODE
