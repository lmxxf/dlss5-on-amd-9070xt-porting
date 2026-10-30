$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\ksearch-round2-20261031'
$s4='D:\DLSSNR-Lab\ksearch-step4-20261031\payload\HIP\gfx1201'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('base','c32-p0','c32-p1','c32-p2','qkv-p0','qkv-p1','qkv-p2')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item "$s4\*" "$r\$arm\modules" -Force}
Copy-Item "$r\payload\c32-p0-c32-wave1-fast.hsaco" "$r\c32-p0\modules\c32-wave1-fast.hsaco" -Force
Copy-Item "$r\payload\c32-p0-c32-wave1-fast-norm900.hsaco" "$r\c32-p0\modules\c32-wave1-fast-norm900.hsaco" -Force
Copy-Item "$r\payload\c32-p1-c32-wave1-fast.hsaco" "$r\c32-p1\modules\c32-wave1-fast.hsaco" -Force
Copy-Item "$r\payload\c32-p1-c32-wave1-fast-norm900.hsaco" "$r\c32-p1\modules\c32-wave1-fast-norm900.hsaco" -Force
Copy-Item "$r\payload\c32-p2-c32-wave1-fast.hsaco" "$r\c32-p2\modules\c32-wave1-fast.hsaco" -Force
Copy-Item "$r\payload\c32-p2-c32-wave1-fast-norm900.hsaco" "$r\c32-p2\modules\c32-wave1-fast-norm900.hsaco" -Force
Copy-Item "$r\compile\gfx1201-qkv-p0\qkv-p0.hsaco" "$r\qkv-p0\modules\vit-stream-fast.hsaco" -Force
Copy-Item "$r\compile\gfx1201-qkv-p1\qkv-p1.hsaco" "$r\qkv-p1\modules\vit-stream-fast.hsaco" -Force
Copy-Item "$r\compile\gfx1201-qkv-p2\qkv-p2.hsaco" "$r\qkv-p2\modules\vit-stream-fast.hsaco" -Force
