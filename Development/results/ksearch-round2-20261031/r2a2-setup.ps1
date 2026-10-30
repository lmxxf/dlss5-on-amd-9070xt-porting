$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-round2-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','r2a2')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\ksearch-round2-20261031\base\modules\*' "$r\$arm\modules" -Force}
Copy-Item "$r\payload\r2a2-vit-stream-fast.hsaco" "$r\r2a2\modules\vit-stream-fast.hsaco"
