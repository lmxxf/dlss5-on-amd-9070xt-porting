$ErrorActionPreference='Stop'
$env:DLSS5_PREBLOCK_LIVE_PROFILE='1'
$env:DLSS5_PREBLOCK_SEED='0'
$Folder='D:\DLSSNR-Lab\matrix-probe\preblock-live-mix'
& "$Folder\preblock-mix-amd.exe" "$Folder\weights.f32" "$Folder\input.rgba32f" "$Folder\oracle.f32" "$Folder\output.f32" "$Folder\preblock_input_mix.hlsl"
if($LASTEXITCODE -ne 0){throw "Probe failed: $LASTEXITCODE"}
