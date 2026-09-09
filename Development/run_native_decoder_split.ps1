param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-decoder40-47',[switch]$C256,[switch]$C128)
$ErrorActionPreference='Stop'
$env:DLSS5_SHADER_PROGRESS='1'
if ($C256 -and $C128) { throw 'Choose one channel mode' }
$mode=if ($C128) { 'decoder57_61' } elseif ($C256) { 'decoder49_55' } else { 'decoder40_47' }
& (Join-Path $Folder 'native-decoder-split-test.exe') $Folder $mode
if ($LASTEXITCODE -ne 0) { throw "Decoder chain failed: $LASTEXITCODE" }
