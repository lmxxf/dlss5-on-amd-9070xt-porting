$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
if(Test-Path D:\DLSSNR-Lab\gpu.lock){throw 'busy'}
$out='D:\給網友打包';$n='Magpie-DLSS5-AMD-0.45';$b="D:\DLSSNR-Lab\release-045\pre-docfix\$n.zip"
if((Get-FileHash $b).Hash -ne (Get-FileHash "$out\$n.zip").Hash){throw 'original ZIP drift'}
$z=[IO.Compression.ZipFile]::OpenRead($b)
try{foreach($name in @('sources/re9-presr-source.tar.gz','release.json','DLSS5-AMD-VERSION.txt','SHA256SUMS.txt')){$matches=@($z.Entries|Where-Object {$_.FullName.Replace('\','/') -eq $name});if($matches.Count -ne 1){throw 'missing original entry'};$e=$matches[0];[IO.Compression.ZipFileExtensions]::ExtractToFile($e,(Join-Path "$out\$n" $name),$true)}}finally{$z.Dispose()}
'RESEAL_STAGE_RESTORED_FROM_VERIFIED_ORIGINAL'
