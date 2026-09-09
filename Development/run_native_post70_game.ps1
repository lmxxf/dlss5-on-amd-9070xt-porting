$ErrorActionPreference='Stop'
Remove-Item Env:DLSS5_POST_BASE_ONLY -ErrorAction SilentlyContinue
$Folder='D:\DLSSNR-Lab\matrix-probe\native-post70-game'
if(Get-Process native-post70-test -ErrorAction SilentlyContinue){throw 'Post test already running'}
& "$Folder\native-post70-test.exe" $Folder game
if($LASTEXITCODE -ne 0){throw "Actual-size post test failed: $LASTEXITCODE"}
