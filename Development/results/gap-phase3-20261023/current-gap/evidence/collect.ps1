$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\gap-phase3-20261023\current-gap'
$e="$r\evidence"
New-Item -ItemType Directory -Force $e|Out-Null
Get-ChildItem $r -File|Where-Object {$_.Extension -in '.json','.ps1'}|Copy-Item -Destination $e
Get-ChildItem $r -Directory|Where-Object {$_.Name -match '^\d\d-r'}|ForEach-Object {
 $dest=Join-Path $e $_.Name;New-Item -ItemType Directory -Force $dest|Out-Null
 Get-ChildItem $_.FullName -File|Where-Object {$_.Extension -in '.log','.csv','.json'}|Copy-Item -Destination $dest
}
Compress-Archive -Path "$e\*" -DestinationPath "$r\evidence.zip" -Force
'COLLECTED'
