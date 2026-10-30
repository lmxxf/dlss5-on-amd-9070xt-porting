$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-k2-k3-20261030'
$d="$r\full-binaries";New-Item -ItemType Directory -Force $d|Out-Null
foreach($a in @('gfx1200','gfx1201')){New-Item -ItemType Directory -Force "$d\$a"|Out-Null;Get-ChildItem "$r\full\compile-$a-*\*.hsaco"|Copy-Item -Destination "$d\$a"}
Compress-Archive -Path "$d\*" -DestinationPath "$r\full-binaries.zip" -Force
