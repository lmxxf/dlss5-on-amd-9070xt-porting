param([int]$Width=64)
$ErrorActionPreference='Stop'
$env:DLSS5_TEST_WIDTH=$Width.ToString()
$Folder='D:\DLSSNR-Lab\matrix-probe\native-c32'
& "$Folder\native-preblock-test.exe" "$Folder\ffn.f32" "$Folder\attention.f32" "$Folder\input.f32" "$Folder\main.f32" "$Folder\down.f32" "$Folder\raw.f32" $Folder c32
if($LASTEXITCODE -ne 0){throw "Native C32 test failed: $LASTEXITCODE"}
