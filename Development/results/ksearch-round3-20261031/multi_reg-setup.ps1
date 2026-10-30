$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\ksearch-round3-20261031'
$b1='D:\DLSSNR-Lab\ksearch-round2-20261031\payload\HIP\gfx1201'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
New-Item -ItemType Directory -Force "$r\r3a_final\modules"|Out-Null
Copy-Item "$b1\*" "$r\r3a_final\modules" -Force
foreach($m in @('c32-wave1-fast.hsaco','c32-wave1-fast-norm900.hsaco','c32-wave1.hsaco','c32-wave1-rtz.hsaco')){
 Copy-Item "$r\payload\r3a-$m" "$r\r3a_final\modules\$m" -Force
}
