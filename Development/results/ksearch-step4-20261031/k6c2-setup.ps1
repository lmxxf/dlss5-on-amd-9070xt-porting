$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-step4-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($arm in @('baseline','k6c2')){New-Item -ItemType Directory -Force "$r\$arm\modules"|Out-Null;Copy-Item 'D:\DLSSNR-Lab\c32-stage-phase5b-20261029\payload\HIP\gfx1201\*' "$r\$arm\modules"}
Copy-Item "$r\payload\k6c2-c64-wave2-fast.hsaco" "$r\k6c2\modules\c64-wave2-fast.hsaco"
Copy-Item "$r\payload\k6c2-c64-wave2.hsaco" "$r\k6c2\modules\c64-wave2.hsaco"
Copy-Item "$r\payload\k6c2-vit-stream-fast.hsaco" "$r\k6c2\modules\vit-stream-fast.hsaco"
Copy-Item "$r\payload\k6c2-vit-stream.hsaco" "$r\k6c2\modules\vit-stream.hsaco"
Copy-Item "$r\payload\k6c2-c512-m32-deep.hsaco" "$r\k6c2\modules\c512-m32-deep.hsaco"
Copy-Item "$r\payload\k6c2-c512-m32-mh.hsaco" "$r\k6c2\modules\c512-m32-mh.hsaco"
Copy-Item "$r\payload\k6c2-deep_fast.hsaco" "$r\k6c2\modules\deep_fast.hsaco"
Copy-Item "$r\payload\k6c2-vit-wide-deep.hsaco" "$r\k6c2\modules\vit-wide-deep.hsaco"
Copy-Item "$r\payload\k6c2-multihead-fast-padded-wave.hsaco" "$r\k6c2\modules\multihead-fast-padded-wave.hsaco"
Copy-Item "$r\payload\k6c2-multihead-fast-padded-wave-packed.hsaco" "$r\k6c2\modules\multihead-fast-padded-wave-packed.hsaco"
Copy-Item "$r\payload\k6c2-deep_fast-packed-fast.hsaco" "$r\k6c2\modules\deep_fast-packed-fast.hsaco"
Copy-Item "$r\payload\k6c2-deep_fast-packed.hsaco" "$r\k6c2\modules\deep_fast-packed.hsaco"
