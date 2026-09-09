param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-upsample48',[switch]$Wide,[switch]$Block56,[switch]$Block62,[switch]$Block66)
$ErrorActionPreference='Stop'
$env:DLSS5_SHADER_PROGRESS='1'
if (($Wide -and $Block56) -or ($Block62 -and ($Wide -or $Block56)) -or ($Block66 -and ($Wide -or $Block56 -or $Block62))) { throw 'Choose one mode' }
$mode=if ($Block66) { 'upsample66' } elseif ($Block62) { 'upsample62' } elseif ($Block56) { 'upsample56' } elseif ($Wide) { 'upsample48wide' } else { 'upsample48' }
$exe=if ($Block66) { 'native-upsample66-test.exe' } elseif ($Block62) { 'native-upsample62-test.exe' } elseif ($Block56) { 'native-upsample56-test.exe' } else { 'native-upsample48-test.exe' }
& (Join-Path $Folder $exe) $Folder $mode
if ($LASTEXITCODE -ne 0) { throw "Upsample test failed: $LASTEXITCODE" }
