param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-post70',[ValidateSet(0,1,2)][int]$Diagnostic=0)
$ErrorActionPreference='Stop'
$env:DLSS5_SHADER_PROGRESS='1'
if ($Diagnostic -eq 0) { Remove-Item Env:DLSS5_POST_BASE_ONLY -ErrorAction SilentlyContinue } else { $env:DLSS5_POST_BASE_ONLY=$Diagnostic.ToString() }
& (Join-Path $Folder 'native-post70-test.exe') $Folder
if ($LASTEXITCODE -ne 0) { throw "Post70 failed: $LASTEXITCODE" }
