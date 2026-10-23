$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\gap-phase6-20261023'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
foreach($kind in @('missing-producer','missing-consumer')){
 $d="$r\knife1\$kind-modules";New-Item -ItemType Directory -Force $d|Out-Null;Copy-Item "$r\knife1\modules\*" $d
 $m=if($kind -eq 'missing-producer'){'vit-stream-fast'}else{'deep_fast-packed-fast'}
 Copy-Item "$r\baseline\modules\$m.hsaco" $d
}
'PAIRED_FALLBACK_MODULES_READY'
