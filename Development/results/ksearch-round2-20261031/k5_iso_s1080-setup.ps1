$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-round2-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('k5_iso_s1080-base','k5_iso_s1080')){
 New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null
 Copy-Item 'D:\DLSSNR-Lab\ksearch-round2-20261031\base\modules\*' "$r\$arm\modules" -Force
 Copy-Item "$r\payload\r2d_both-c32-wave1-fast.hsaco" "$r\$arm\modules\c32-wave1-fast.hsaco" -Force
 Copy-Item "$r\payload\r2d_both-c32-wave1-fast-norm900.hsaco" "$r\$arm\modules\c32-wave1-fast-norm900.hsaco" -Force
 Copy-Item "$r\payload\r2d_both-swin-persistent.hsaco" "$r\$arm\modules\swin-persistent.hsaco" -Force
 Copy-Item "$r\payload\r2d_both-swin-persistent-fast.hsaco" "$r\$arm\modules\swin-persistent-fast.hsaco" -Force
}
Copy-Item "$r\payload\k5_iso_s1080-base-c64-wave2-fast.hsaco" "$r\k5_iso_s1080-base\modules\c64-wave2-fast.hsaco" -Force
Copy-Item "$r\payload\k5_iso_s1080-cand-c64-wave2-fast.hsaco" "$r\k5_iso_s1080\modules\c64-wave2-fast.hsaco" -Force
