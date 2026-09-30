param([string[]]$Modules=@('vit-stream'))
$ErrorActionPreference='Stop';$root='D:\DLSSNR-Lab\hip-backend\vit-1080-gap-20261001'
if(Test-Path "$root\src"){Remove-Item "$root\src" -Recurse -Force};Expand-Archive "$root\src.zip" "$root\src" -Force
$Modules=@($Modules|ForEach-Object{$_ -split ','}|Where-Object{$_})
foreach($m in $Modules){foreach($arch in 'gfx1200','gfx1201'){
 & "$root\src\build-modules.ps1" -SourceDir "$root\src" -OutputDir "$root\build-final\$arch" -Compiler 'D:\DLSSNR-Lab\build-0927\rtc_compile.exe' -Targets $arch -Only $m
 if(!$?){throw 'compile failed'}}}
Get-ChildItem "$root\build-final" -Recurse -Filter '*.hsaco'|ForEach-Object{"$($_.FullName.Substring($root.Length+1)) $((Get-FileHash $_.FullName).Hash)"}
'FINAL_DONE'
