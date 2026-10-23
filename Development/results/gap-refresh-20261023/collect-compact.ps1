$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\gap-refresh-20261023\compact1088';$e="$r\evidence"
New-Item -ItemType Directory -Force $e|Out-Null
Get-ChildItem $r -File|Where-Object {$_.Extension -eq '.json'}|Copy-Item -Destination $e
Get-ChildItem $r -Directory|Where-Object {$_.Name -ne 'evidence'}|ForEach-Object {$dest=Join-Path $e $_.Name;New-Item -ItemType Directory -Force $dest|Out-Null;Get-ChildItem $_.FullName -File|Where-Object {$_.Extension -in '.log','.json','.csv'}|Copy-Item -Destination $dest}
Compress-Archive -Path "$e\*" -DestinationPath "$r\evidence.zip" -Force
'COLLECTED'
