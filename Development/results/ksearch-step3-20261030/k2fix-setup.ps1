$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-step3-20261030'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','k2fix')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\c32-stage-phase5b-20261029\payload\HIP\gfx1201\*' "$r\$arm\modules"}
Copy-Item "$r\payload\k2fix-vit-stream-fast.hsaco" "$r\k2fix\modules\vit-stream-fast.hsaco"
Copy-Item "$r\payload\k2fix-vit-stream.hsaco" "$r\k2fix\modules\vit-stream.hsaco"
Copy-Item "$r\payload\k2fix-c512-m32-deep.hsaco" "$r\k2fix\modules\c512-m32-deep.hsaco"
Copy-Item "$r\payload\k2fix-c512-m32-mh.hsaco" "$r\k2fix\modules\c512-m32-mh.hsaco"
Copy-Item "$r\payload\k2fix-deep_fast.hsaco" "$r\k2fix\modules\deep_fast.hsaco"
Copy-Item "$r\payload\k2fix-vit-wide-deep.hsaco" "$r\k2fix\modules\vit-wide-deep.hsaco"
Copy-Item "$r\payload\k2fix-multihead-fast-padded-wave.hsaco" "$r\k2fix\modules\multihead-fast-padded-wave.hsaco"
Copy-Item "$r\payload\k2fix-multihead-fast-padded-wave-packed.hsaco" "$r\k2fix\modules\multihead-fast-padded-wave-packed.hsaco"
Copy-Item "$r\payload\k2fix-deep_fast-packed-fast.hsaco" "$r\k2fix\modules\deep_fast-packed-fast.hsaco"
Copy-Item "$r\payload\k2fix-deep_fast-packed.hsaco" "$r\k2fix\modules\deep_fast-packed.hsaco"
