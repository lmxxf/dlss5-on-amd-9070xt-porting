$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-round3-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','r3b')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\ksearch-round3-20261031\base\modules\*' "$r\$arm\modules"}
Copy-Item "$r\payload\r3b-vit-stream-fast.hsaco" "$r\r3b\modules\vit-stream-fast.hsaco"
Copy-Item "$r\payload\r3b-vit-stream.hsaco" "$r\r3b\modules\vit-stream.hsaco"
