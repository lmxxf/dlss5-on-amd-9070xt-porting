# Build variants (recipe + extra defines) of one module for both arches. -Name X -Module m -Defs 'A 1','B 2'
param([string]$Name,[string]$Module,[string[]]$Defs=@())
$ErrorActionPreference='Stop';$root='D:\DLSSNR-Lab\hip-backend\c256-gap-20261001'
$Defs=@($Defs|ForEach-Object{$_ -split ","}|Where-Object{$_}|ForEach-Object{$_ -replace "=", " "})
if(!(Test-Path "$root\src")){Expand-Archive "$root\src.zip" "$root\src" -Force}
foreach($arch in 'gfx1200','gfx1201'){
 & "$root\src\build-modules.ps1" -SourceDir "$root\src" -OutputDir "$root\build-$Name\$arch" -Compiler 'D:\DLSSNR-Lab\build-0927\rtc_compile.exe' -Targets $arch -Only $Module -ExtraDefines $Defs
 if(!$?){throw 'compile failed'}
 $f=Get-ChildItem "$root\build-$Name\$arch" -Recurse -Filter "$Module.hsaco"|Select-Object -First 1
 "$arch $Name $Module $((Get-FileHash $f.FullName).Hash) installed $((Get-FileHash "$root\baseline\$arch\$Module.hsaco").Hash)"}
'BUILD_DONE'
