$ErrorActionPreference='Stop'
Get-ChildItem D:\DLSSNR-Lab -Directory|Where-Object {$_.Name -match '310|small|mochi|008'}|Select-Object -ExpandProperty FullName
Get-ChildItem D:\DLSSNR-Lab\small-model-20261018\assets -File|Select-Object -First 10 -ExpandProperty Name
Get-ChildItem D:\DLSSNR-Lab -Filter 'dlssnr-small.bin' -Recurse -ErrorAction SilentlyContinue|Select-Object FullName,Length
