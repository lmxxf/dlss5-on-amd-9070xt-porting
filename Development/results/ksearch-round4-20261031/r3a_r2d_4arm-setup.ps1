$ErrorActionPreference="Stop"
$r="D:\DLSSNR-Lab\ksearch-round4-20261031"
$b1="$r\base\modules"
if(Test-Path "D:\DLSSNR-Lab\gpu.lock"){throw "GPU busy"}
foreach($arm in @("A0_NoR2D","A1_R2D_C32","A2_R2D_MH","A3_R2D_Both")){
 New-Item -ItemType Directory -Force "$r\arm4-$arm\modules"|Out-Null
 Copy-Item "$b1\*" "$r\arm4-$arm\modules" -Force
}
foreach($m in @("c32-wave1-fast.hsaco","c32-wave1-fast-norm900.hsaco","c32-wave1.hsaco","c32-wave1-rtz.hsaco")){
 Copy-Item "$r\payload\r3a_nor2d-$m" "$r\arm4-A0_NoR2D\modules\$m" -Force
 Copy-Item "$r\payload\r3a_nor2d-$m" "$r\arm4-A2_R2D_MH\modules\$m" -Force
}
foreach($m in @("c64-wave2-fast.hsaco","c64-wave2.hsaco","swin-persistent.hsaco","swin-persistent-fast.hsaco")){
 Copy-Item "$r\payload\e7b2-$m" "$r\arm4-A0_NoR2D\modules\$m" -Force
 Copy-Item "$r\payload\e7b2-$m" "$r\arm4-A1_R2D_C32\modules\$m" -Force
}
