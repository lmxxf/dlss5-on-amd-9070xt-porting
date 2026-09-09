$ErrorActionPreference='Stop'
$Folder='D:\DLSSNR-Lab\native-repack640'
$env:DLSS5_VIT_CUBIN=Join-Path $Folder 'dlssnr-05.cubin'
& "$Folder\native-repack.exe" "$Folder\forward.i32" "$Folder\forward.json" 32 20
if($LASTEXITCODE -ne 0){throw "Repack failed: $LASTEXITCODE"}
& "$Folder\native-repack.exe" "$Folder\inverse.i32" "$Folder\inverse.json" 32 20 --inverse
if($LASTEXITCODE -ne 0){throw "Inverse repack failed: $LASTEXITCODE"}
