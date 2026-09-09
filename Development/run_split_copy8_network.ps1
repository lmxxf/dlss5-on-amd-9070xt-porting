param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\bin\x64\dxc.exe'
$Inc='D:\DLSSNR-Lab\matrix-probe\dxc-preview\inc\hlsl'
# FAST PATH: C512 stage-1 projection also writes the E4M3 copy (u1); the per-block pack dispatch is skipped.
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=512 -D RAW=0 -D NATIVE_FAST_ACCUMULATE=1 -D NATIVE_FP8_OPERANDS=1 -D NATIVE_FP8_COPY=1 native_wave_project.hlsl -Fo native_wave_project_copy8_c512.cso
if($LASTEXITCODE -ne 0){throw 'split copy8 projection compilation failed'}
$env:DLSS5_SPLIT_COPY8='1'
& "$Folder\run_preblock_main8_network.ps1" -Folder $Folder
exit $LASTEXITCODE
