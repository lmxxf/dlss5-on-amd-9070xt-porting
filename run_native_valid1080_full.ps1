param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-valid1080-full',
      [string]$Noise='D:\DLSSNR-Lab\matrix-probe\native-runtime-rgb512\functions.f32')
$ErrorActionPreference='Stop'
if(Get-Process native-preblock-test -ErrorAction SilentlyContinue){throw 'An encoder test is already running; do not restart it'}
if((Get-Item $Noise).Length -ne 201326592){throw 'Noise table size mismatch'}
Remove-Item Env:DLSS5_POST_BASE_ONLY -ErrorAction SilentlyContinue
$env:DLSS5_FRONTFINAL='1'
$env:DLSS5_REFLECT_VALID1080='1'
$env:DLSS5_TEST_WIDTH='1920'
$env:DLSS5_TEST_SEED='0'
$env:DLSS5_NOISE_TABLE=$Noise
$env:DLSS5_SHADER_PROGRESS='1'
& "$Folder\native-preblock-test.exe" "$Folder\block0-ffn.f32" "$Folder\block0-attention.f32" "$Folder\input.f32" "$Folder\gpu-main.f32" "$Folder\gpu-down.f32" "$Folder\gpu-raw.f32" $Folder live global
if($LASTEXITCODE -ne 0){throw "Full chain failed: $LASTEXITCODE"}
