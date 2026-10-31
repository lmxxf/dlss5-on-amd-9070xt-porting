$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-round4-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','r4a')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\ksearch-round4-20261031\base\modules\*' "$r\$arm\modules"}
Copy-Item "$r\payload\r4a-c32-wave1.hsaco" "$r\r4a\modules\c32-wave1.hsaco"
Copy-Item "$r\payload\r4a-c32-wave1-rtz.hsaco" "$r\r4a\modules\c32-wave1-rtz.hsaco"
Copy-Item "$r\payload\r4a-c32-wave1-fast.hsaco" "$r\r4a\modules\c32-wave1-fast.hsaco"
Copy-Item "$r\payload\r4a-c32-wave1-fast-norm900.hsaco" "$r\r4a\modules\c32-wave1-fast-norm900.hsaco"
Copy-Item "$r\payload\r4a-c64-wave2.hsaco" "$r\r4a\modules\c64-wave2.hsaco"
Copy-Item "$r\payload\r4a-c64-wave2-fast.hsaco" "$r\r4a\modules\c64-wave2-fast.hsaco"
Copy-Item "$r\payload\r4a-swin-persistent.hsaco" "$r\r4a\modules\swin-persistent.hsaco"
Copy-Item "$r\payload\r4a-swin-persistent-fast.hsaco" "$r\r4a\modules\swin-persistent-fast.hsaco"
Copy-Item "$r\payload\r4a-c512-m32-mh.hsaco" "$r\r4a\modules\c512-m32-mh.hsaco"
