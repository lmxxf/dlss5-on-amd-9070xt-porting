param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-decoder40-47')
$ErrorActionPreference='Stop'
$env:DLSS5_SHADER_PROGRESS='1'
& (Join-Path $Folder 'native-decoder-split-test.exe') $Folder decoder40_47
if ($LASTEXITCODE -ne 0) { throw "Decoder chain failed: $LASTEXITCODE" }
