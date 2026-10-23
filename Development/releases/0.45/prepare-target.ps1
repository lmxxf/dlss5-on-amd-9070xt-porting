$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\release-045'
if((Get-PSDrive D).Free -lt 100GB){throw 'disk low'}
if((Get-FileHash "$r\payload045.zip").Hash.ToLower() -ne 'ea107e3f2b16e48bfadb2d016e8ba3c8bb107420470f6903a0bed26a0741d947'){throw 'payload archive mismatch'}
Expand-Archive "$r\payload045.zip" $r -Force
foreach($f in (Get-Content "$r\payload-expected.json" -Raw|ConvertFrom-Json)){if((Get-FileHash (Join-Path $r $f.relative)).Hash.ToLower() -ne $f.sha256){throw 'payload expanded hash mismatch'}}
$smoke='D:\DLSSNR-Lab\release-044-f9fix\payload\runtime-smoke.exe'
if((Get-FileHash $smoke).Hash.ToLower() -ne '2ffe128b6d1c24b1aab33f851ee47d2b2abe62fce10364bd628fc3ca23f13014'){throw 'smoke mismatch'}
Copy-Item $smoke "$r\payload\runtime-smoke.exe" -Force
'PAYLOAD_045_VERIFIED'
