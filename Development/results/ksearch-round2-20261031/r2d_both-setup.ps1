$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-round2-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','r2d_both')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\ksearch-round2-20261031\base\modules\*' "$r\$arm\modules"}
Copy-Item "$r\payload\r2d_both-c32-wave1-fast.hsaco" "$r\r2d_both\modules\c32-wave1-fast.hsaco"
Copy-Item "$r\payload\r2d_both-c32-wave1-fast-norm900.hsaco" "$r\r2d_both\modules\c32-wave1-fast-norm900.hsaco"
Copy-Item "$r\payload\r2d_both-c64-wave2-fast.hsaco" "$r\r2d_both\modules\c64-wave2-fast.hsaco"
Copy-Item "$r\payload\r2d_both-swin-persistent.hsaco" "$r\r2d_both\modules\swin-persistent.hsaco"
Copy-Item "$r\payload\r2d_both-swin-persistent-fast.hsaco" "$r\r2d_both\modules\swin-persistent-fast.hsaco"
