$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\release-045';$d="$r\receipts";New-Item -ItemType Directory -Force $d|Out-Null
Get-ChildItem $r -File|Where-Object {$_.Extension -in '.json','.log','.sha256'}|ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $d -Force}
Compress-Archive -Path "$d\*" -DestinationPath "$r\receipts.zip" -Force
'RECEIPTS_READY'
