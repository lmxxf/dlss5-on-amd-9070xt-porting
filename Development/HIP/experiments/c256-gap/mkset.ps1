# mkset -Name X -Builds b1,b2 : flat-X = flat-A with the gfx1201 module(s) of build-b1, build-b2 ...
param([string]$Name,[string[]]$Builds)
$ErrorActionPreference='Stop';$root='D:\DLSSNR-Lab\hip-backend\c256-gap-20261001'
$Builds=@($Builds|ForEach-Object{$_ -split ","}|Where-Object{$_})
New-Item -ItemType Directory -Force "$root\flat-$Name"|Out-Null;Copy-Item "$root\flat-A\*.hsaco" "$root\flat-$Name" -Force
foreach($b in $Builds){Get-ChildItem "$root\build-$b\gfx1201" -Recurse -Filter '*.hsaco'|ForEach-Object{Copy-Item $_.FullName "$root\flat-$Name" -Force;"$Name <- $b $($_.Name) $((Get-FileHash $_.FullName).Hash)"}}
