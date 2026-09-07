param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 native_wave_c32_ffn_blocked.hlsl -Fo native_wave_c32_ffn_blocked.cso
if($LASTEXITCODE -ne 0){throw 'Blocked C32 FFN compilation failed'}
$env:DLSS5_TEST_BLOCKED_C32_FFN='1'
& "$Folder\run_split_ffwd_blocked_network.ps1" -Folder $Folder
exit $LASTEXITCODE
