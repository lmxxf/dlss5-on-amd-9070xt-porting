$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\ksearch-round3-20261031'
$b1='D:\DLSSNR-Lab\ksearch-round2-20261031\payload\HIP\gfx1201'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
New-Item -ItemType Directory -Force "$r\base\modules"|Out-Null
Copy-Item "$b1\*" "$r\base\modules" -Force
New-Item -ItemType Directory -Force "$r\baseline\modules"|Out-Null
Copy-Item "$b1\*" "$r\baseline\modules" -Force
foreach($arm in @('P0','P1','P2')){
 New-Item -ItemType Directory -Force "$r\sm-$arm\modules"|Out-Null
 Copy-Item "$b1\*" "$r\sm-$arm\modules" -Force
 Copy-Item "$r\payload\sm-$arm-c32-wave1-fast.hsaco" "$r\sm-$arm\modules\c32-wave1-fast.hsaco" -Force
 Copy-Item "$r\payload\sm-$arm-c32-wave1-fast-norm900.hsaco" "$r\sm-$arm\modules\c32-wave1-fast-norm900.hsaco" -Force
}
