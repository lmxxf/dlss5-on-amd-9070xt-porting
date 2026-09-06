param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-vit-chain')
$ErrorActionPreference='Stop'
$env:DLSS5_VIT_SWITCH_INPUT='1'
& (Join-Path $Folder 'native-vit-block-test.exe') $Folder vit_chain
if ($LASTEXITCODE -ne 0) { throw "ViT chain failed: $LASTEXITCODE" }
