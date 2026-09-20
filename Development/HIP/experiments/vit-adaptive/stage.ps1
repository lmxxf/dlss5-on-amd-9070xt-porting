$ErrorActionPreference='Stop';$lab='D:\DLSSNR-Lab';$dest="$lab\AttExp-preview";$payload="$dest\payload";New-Item -ItemType Directory -Force "$payload\DLSS5-AMD\native-game-tiled-assets\HIP"|Out-Null
# Remove only the obsolete staging directory made by the previous installer.
if(Test-Path "$payload\DLSS5-AMD\HIP"){Remove-Item "$payload\DLSS5-AMD\HIP" -Recurse -Force}
Copy-Item "$lab\hip-backend\native-vit-adaptive.addon64" "$payload\dlss5-amd.addon64" -Force
Copy-Item "$lab\hip-backend\vit-residual-scales\gain.f32" "$payload\DLSS5-AMD\vit-reuse-gain.f32" -Force
foreach($arch in 'gfx1200','gfx1201'){
 $out="$payload\DLSS5-AMD\native-game-tiled-assets\HIP\$arch";New-Item -ItemType Directory -Force $out|Out-Null
 Copy-Item "$lab\c256-frag-production-modules\$arch\*.hsaco" $out -Force
 Copy-Item "$lab\vit-residual-adaptive-build\$arch\deep_fast-packed.hsaco" $out -Force
 if(@(Get-ChildItem $out -Filter *.hsaco).Count -ne 24){throw 'Architecture module count'}
 if($arch -eq 'gfx1201'){foreach($f in Get-ChildItem $out -Filter *.hsaco){if((Get-FileHash $f.FullName).Hash -ne (Get-FileHash "$lab\hip-backend\vit-residual-adaptive-modules\$($f.Name)").Hash){throw "Staged module differs from benchmark: $($f.Name)"}}}
}
Copy-Item "$lab\hip-backend\install-adaptive-preview.ps1" "$dest\install-preview.ps1" -Force
$files=@(Get-ChildItem $payload -Recurse -File|ForEach-Object{[pscustomobject]@{name=$_.FullName.Substring($payload.Length+1);sha256=(Get-FileHash $_.FullName).Hash;bytes=$_.Length}})
if($files.Count -ne 50){throw 'Payload file count'};$files|ConvertTo-Json|Set-Content "$dest\manifest.json"
Write-Output "STAGED $dest : $($files.Count) files. Game installation untouched."
