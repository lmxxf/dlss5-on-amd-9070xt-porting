# vit-1080-gap-20261001: flat-A = installed Stellar Blade 31 gfx1201 modules (+ baseline both arches); regression.ps1 from c512-compact-vt; hosts + src.zip uploaded.
$ErrorActionPreference='Stop'
$root='D:\DLSSNR-Lab\hip-backend\vit-1080-gap-20261001';$prev='D:\DLSSNR-Lab\hip-backend\c512-compact-vt-20260930'
$game='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
New-Item -ItemType Directory -Force "$root\flat-A","$root\baseline\gfx1200","$root\baseline\gfx1201"|Out-Null
$rows=@()
foreach($arch in 'gfx1200','gfx1201'){$files=@(Get-ChildItem "$game\DLSS5-AMD\native-game-tiled-assets\HIP\$arch" -Filter '*.hsaco');if($files.Count -ne 31){throw 'Expected 31'}
 foreach($f in $files){Copy-Item $f.FullName "$root\baseline\$arch\$($f.Name)" -Force;$rows+=@{path=$f.FullName;sha256=(Get-FileHash $f.FullName).Hash}}}
foreach($f in @("$game\dlss5-amd.addon64","$game\dxgi.dll","$game\OptiScaler.ini","$game\DLSS5-AMD\native-game-flags.txt")){$rows+=@{path=$f;sha256=(Get-FileHash $f).Hash}}
$rows|ConvertTo-Json -Depth 5|Set-Content "$root\snapshot.json"
Copy-Item "$root\baseline\gfx1201\*.hsaco" "$root\flat-A" -Force
(Get-Content "$prev\regression.ps1" -Raw).Replace('c512-compact-vt-20260930','vit-1080-gap-20261001')|Set-Content "$root\regression.ps1"
if(Test-Path "$root\src"){Remove-Item "$root\src" -Recurse -Force};Expand-Archive "$root\src.zip" "$root\src" -Force
"addon $((Get-FileHash "$game\dlss5-amd.addon64").Hash)";'SETUP_DONE'
