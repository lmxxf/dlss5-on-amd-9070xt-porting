$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\gap-phase6-20261023'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
New-Item -ItemType Directory -Force "$r\knife1\modules"|Out-Null
Copy-Item "$r\baseline\modules\*" "$r\knife1\modules"
foreach($m in @('deep_fast-packed-fast','vit-stream-fast')){
 if((Get-FileHash "$r\baseline\modules\$m.hsaco").Hash -ne (Get-FileHash "$r\knife1\compile-baseline-$m\$m.hsaco").Hash){throw 'baseline changed'}
 Copy-Item "$r\knife1\compile-candidate-$m\$m.hsaco" "$r\knife1\modules"
}
'PAIRED_MODULES_READY'
