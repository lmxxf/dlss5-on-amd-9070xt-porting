param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-upsample48',[switch]$Wide)
$ErrorActionPreference='Stop'
$env:DLSS5_SHADER_PROGRESS='1'
$mode=if ($Wide) { 'upsample48wide' } else { 'upsample48' }
& (Join-Path $Folder 'native-upsample48-test.exe') $Folder $mode
if ($LASTEXITCODE -ne 0) { throw "Upsample48 test failed: $LASTEXITCODE" }
