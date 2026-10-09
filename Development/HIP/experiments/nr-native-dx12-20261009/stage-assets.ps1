$ErrorActionPreference='Stop';$src='D:\DLSSNR-Lab\native-game-tiled-assets';$dst='D:\DLSSNR-Lab\nr-dx12-pure-20261009\assets';$noiseRoot='D:\DLSSNR-Lab\history-trial-041a-20261006\assets'
New-Item -ItemType Directory -Force $dst|Out-Null
Get-ChildItem $src -File -Recurse|ForEach-Object{$relative=$_.FullName.Substring($src.Length).TrimStart('\');$target=Join-Path $dst $relative;New-Item -ItemType Directory -Force (Split-Path $target)|Out-Null;if(!(Test-Path $target)){Copy-Item -Force $_.FullName $target}}
$noise=Get-ChildItem $noiseRoot -File|Where-Object {$_.Name -in @('noise.f16','noise.f32')};if(!$noise){throw 'actual noise asset missing'}
$noise|ForEach-Object{Copy-Item -Force $_.FullName (Join-Path $dst $_.Name)}
$noise|ForEach-Object{Get-FileHash $_.FullName}|ConvertTo-Json|Set-Content 'D:\DLSSNR-Lab\nr-dx12-pure-20261009\noise-manifest.json'
'ISOLATED_ASSETS_READY'
