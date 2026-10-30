$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\ksearch-round3-20261031'
$b1='D:\DLSSNR-Lab\ksearch-round2-20261031\payload\HIP\gfx1201'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('Base_K5','K5_R2D_C32','K5_R2D_MH','K5_R2D_Both')){
 New-Item -ItemType Directory -Force "$r\s1080-$arm\modules"|Out-Null
 Copy-Item "$b1\*" "$r\s1080-$arm\modules" -Force
}
foreach($m in @('c32-wave1-fast.hsaco','c32-wave1-fast-norm900.hsaco','c32-wave1.hsaco','c32-wave1-rtz.hsaco')){
 Copy-Item "$r\payload\e7b2-$m" "$r\s1080-Base_K5\modules\$m" -Force
 Copy-Item "$r\payload\e7b2-$m" "$r\s1080-K5_R2D_MH\modules\$m" -Force
}
foreach($m in @('c64-wave2-fast.hsaco','c64-wave2.hsaco','swin-persistent.hsaco','swin-persistent-fast.hsaco')){
 Copy-Item "$r\payload\e7b2-$m" "$r\s1080-Base_K5\modules\$m" -Force
 Copy-Item "$r\payload\e7b2-$m" "$r\s1080-K5_R2D_C32\modules\$m" -Force
}
