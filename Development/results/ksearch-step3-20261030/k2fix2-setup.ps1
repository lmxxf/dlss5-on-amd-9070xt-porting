$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-step3-20261030'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','k2fix2')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\c32-stage-phase5b-20261029\payload\HIP\gfx1201\*' "$r\$arm\modules"}
Copy-Item "$r\payload\k2fix2-vit-stream-fast.hsaco" "$r\k2fix2\modules\vit-stream-fast.hsaco"
Copy-Item "$r\payload\k2fix2-vit-stream.hsaco" "$r\k2fix2\modules\vit-stream.hsaco"
Copy-Item "$r\payload\k2fix2-c512-m32-deep.hsaco" "$r\k2fix2\modules\c512-m32-deep.hsaco"
Copy-Item "$r\payload\k2fix2-c512-m32-mh.hsaco" "$r\k2fix2\modules\c512-m32-mh.hsaco"
Copy-Item "$r\payload\k2fix2-deep_fast.hsaco" "$r\k2fix2\modules\deep_fast.hsaco"
Copy-Item "$r\payload\k2fix2-vit-wide-deep.hsaco" "$r\k2fix2\modules\vit-wide-deep.hsaco"
Copy-Item "$r\payload\k2fix2-multihead-fast-padded-wave.hsaco" "$r\k2fix2\modules\multihead-fast-padded-wave.hsaco"
Copy-Item "$r\payload\k2fix2-multihead-fast-padded-wave-packed.hsaco" "$r\k2fix2\modules\multihead-fast-padded-wave-packed.hsaco"
Copy-Item "$r\payload\k2fix2-deep_fast-packed-fast.hsaco" "$r\k2fix2\modules\deep_fast-packed-fast.hsaco"
Copy-Item "$r\payload\k2fix2-deep_fast-packed.hsaco" "$r\k2fix2\modules\deep_fast-packed.hsaco"
