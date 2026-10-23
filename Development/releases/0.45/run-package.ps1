param([Parameter(Mandatory=$true)][string]$SourceCommit)
# Execute only inside the root agent's held gpu.lock/watchdog queue. Does not acquire a second lock.
if($SourceCommit -ne 'c24e3a97cdf596d0c997cee7deda91c981d520ce'){throw 'Source snapshot base identity mismatch'}
$RuntimeSmokeSha256='2ffe128b6d1c24b1aab33f851ee47d2b2abe62fce10364bd628fc3ca23f13014'
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\release-045'
if(!(Test-Path 'D:\DLSSNR-Lab\gpu.lock')){throw 'Root-owned gpu.lock required'}
& 'D:\DLSSNR-Lab\game-check.ps1' 'SB-Win64 Onimusha re9.exe SandFall Magpie'
if($LASTEXITCODE -ne 1){throw 'game busy'}
if([IO.DriveInfo]::new('D:\').AvailableFreeSpace -lt 100GB){throw 'disk below100GB'}
if((Get-FileHash "$r\re9-presr-source.tar.gz").Hash -ne '3bcfa93db081f139b39170ed2d9c23da35b34759fbfb5e5593c709da971c829c'){throw 'Release input hash mismatch: re9-presr-source.tar.gz'}
if((Get-FileHash "$r\config045.zip").Hash -ne '714c90db914f564cbf5db8241cd85282e16ed19ea4c896cb75daae0a4a0a5ace'){throw 'Release input hash mismatch: config045.zip'}
if((Get-FileHash "$r\source045.zip").Hash -ne 'aae9adb451021730bb457cf719cccb913d403cce9ca090cf52f52c43293820ae'){throw 'Release input hash mismatch: source045.zip'}
$smokeSource='D:\DLSSNR-Lab\release-044-f9fix\payload\runtime-smoke.exe'
if((Get-Item $smokeSource).Length -ne 649179 -or (Get-FileHash $smokeSource).Hash -ne $RuntimeSmokeSha256){throw 'Pinned existing runtime smoke mismatch'}
New-Item -ItemType Directory "$r\payload" -Force|Out-Null
Copy-Item $smokeSource "$r\payload\runtime-smoke.exe" -Force
if((Get-FileHash "$r\payload\runtime-smoke.exe").Hash -ne $RuntimeSmokeSha256){throw 'Copied runtime smoke mismatch'}
[pscustomobject]@{source=$smokeSource;sha256=$RuntimeSmokeSha256;bytes=649179}|ConvertTo-Json|Set-Content "$r\runtime-smoke-identity.json"
Expand-Archive "$r\config045.zip" "$r\scripts" -Force
Expand-Archive "$r\source045.zip" "$r\source" -Force
& "$r\package-045.ps1" -SourceCommit $SourceCommit
if(!$?){throw 'package failed'}
