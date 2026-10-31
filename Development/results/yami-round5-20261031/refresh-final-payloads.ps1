$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'busy'}
Expand-Archive -Path "$r\refresh-base.zip" -DestinationPath "$r\final-base\modules" -Force
Expand-Archive -Path "$r\refresh-norm.zip" -DestinationPath "$r\final-norm\modules" -Force
Expand-Archive -Path "$r\refresh-c256.zip" -DestinationPath "$r\final-c256\modules" -Force
Expand-Archive -Path "$r\refresh-full.zip" -DestinationPath "$r\final-full\modules" -Force
