$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-round3-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','r3a')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\ksearch-round3-20261031\base\modules\*' "$r\$arm\modules"}
Copy-Item "$r\payload\r3a-c32-wave1-fast.hsaco" "$r\r3a\modules\c32-wave1-fast.hsaco"
Copy-Item "$r\payload\r3a-c32-wave1-fast-norm900.hsaco" "$r\r3a\modules\c32-wave1-fast-norm900.hsaco"
Copy-Item "$r\payload\r3a-c32-wave1.hsaco" "$r\r3a\modules\c32-wave1.hsaco"
Copy-Item "$r\payload\r3a-c32-wave1-rtz.hsaco" "$r\r3a\modules\c32-wave1-rtz.hsaco"
