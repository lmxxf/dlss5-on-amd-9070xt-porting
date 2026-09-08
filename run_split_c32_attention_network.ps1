param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# Four-pass C32 attention: qkv / normalize / attention / projection, Q/K/V read by wave loads.
$Entries=@('qkv','normalize','attention','projection')
for($i=0;$i -lt 4;$i++){
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E $Entries[$i] -HV 2021 -enable-16bit-types -O3 -D "PASS=$i" -D RAW_OUTPUT=1 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" native_wave_c32_split_attention.hlsl -Fo "native_wave_c32_split_$($Entries[$i]).cso"
 if($LASTEXITCODE -ne 0){throw "Split C32 attention pass $i compilation failed"}
}
$env:DLSS5_TEST_SPLIT_C32_ATTENTION='1'
& "$Folder\run_direct_attention_network.ps1" -Folder $Folder
exit $LASTEXITCODE
