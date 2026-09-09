param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-block640',[ValidateSet('block31','chain31_38')][string]$Mode='block31')
$ErrorActionPreference='Stop'
if(Get-Process native-attention-extent -ErrorAction SilentlyContinue){throw 'Test already running'}
$env:DLSS5_VIT_STAGED='1'
& "$Folder\native-attention-extent.exe" $Folder $Mode
if($LASTEXITCODE -ne 0){throw "Staged test failed: $LASTEXITCODE"}
