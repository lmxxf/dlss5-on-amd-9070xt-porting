$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\c32-stage-phase5-20261029'
$d="$r\full-binaries";New-Item -ItemType Directory -Force $d|Out-Null
foreach($a in @('gfx1200','gfx1201')){New-Item -ItemType Directory -Force "$d\$a"|Out-Null;Get-ChildItem "$r\full\compile-$a-*\*.hsaco"|Copy-Item -Destination "$d\$a"}
Compress-Archive -Path "$d\*" -DestinationPath "$r\full-binaries.zip" -Force
