param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-upsample48')
$ErrorActionPreference='Stop'
$env:DLSS5_SHADER_PROGRESS='1'
& (Join-Path $Folder 'native-upsample48-test.exe') $Folder upsample48
if ($LASTEXITCODE -ne 0) { throw "Upsample48 test failed: $LASTEXITCODE" }
