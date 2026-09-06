param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-upsample48',[switch]$Wide,[switch]$Block56)
$ErrorActionPreference='Stop'
$env:DLSS5_SHADER_PROGRESS='1'
if ($Wide -and $Block56) { throw 'Choose -Wide or -Block56' }
$mode=if ($Block56) { 'upsample56' } elseif ($Wide) { 'upsample48wide' } else { 'upsample48' }
$exe=if ($Block56) { 'native-upsample56-test.exe' } else { 'native-upsample48-test.exe' }
& (Join-Path $Folder $exe) $Folder $mode
if ($LASTEXITCODE -ne 0) { throw "Upsample48 test failed: $LASTEXITCODE" }
