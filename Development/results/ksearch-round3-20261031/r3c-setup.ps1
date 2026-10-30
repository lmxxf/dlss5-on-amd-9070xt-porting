$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-round3-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','r3c')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\ksearch-round3-20261031\base\modules\*' "$r\$arm\modules"}
Copy-Item "$r\payload\r3c-c64-wave2-fast.hsaco" "$r\r3c\modules\c64-wave2-fast.hsaco"
Copy-Item "$r\payload\r3c-c64-wave2.hsaco" "$r\r3c\modules\c64-wave2.hsaco"
