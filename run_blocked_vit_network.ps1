param([Parameter(Mandatory=$true)][string]$Folder,[int]$BlockN=4)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# Register-blocked ViT expand/reduce with f16 hidden layer, single dispatch per stage.
& $Dxc -I $Inc -T cs_6_10 -E expand -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=1 -D "BLOCK_N=$BlockN" native_wave_vit_blocked.hlsl -Fo native_wave_vit_expand_blocked.cso
if($LASTEXITCODE -ne 0){throw 'Blocked ViT expand compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D "BLOCK_N=$BlockN" native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_blocked.cso
if($LASTEXITCODE -ne 0){throw 'Blocked ViT reduce compilation failed'}
$env:DLSS5_TEST_BLOCKED_VIT='1'
& "$Folder\run_blocked_ffn_network.ps1" -Folder $Folder
exit $LASTEXITCODE
