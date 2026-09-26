# Stage D:\DLSSNR-Lab\re9-runtime-flags-20260926: new\ (new runtime + 0.31 RE9 assets with the vec-input c32-wave1 +
# repo hip-re9-flags.txt) and old\ (0.31 package runtime 6e9974d7 on the same assets through a junction).
$ErrorActionPreference='Stop'
$lab='D:\DLSSNR-Lab\re9-runtime-flags-20260926'
$src='D:\DLSSNR-Lab\amdnr-0332\ours\DLSS5-AMD\native-game-tiled-assets'   # 0.31 RE9 package assets (ASCII path copy)
$stellar='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64\DLSS5-AMD\native-game-tiled-assets\HIP'
$assets="$lab\new\DLSS5-AMD\native-game-tiled-assets"
if(!(Test-Path "$assets\HIP\SHA256SUMS")){ New-Item -ItemType Directory -Force "$lab\new\DLSS5-AMD"|Out-Null; Copy-Item $src $assets -Recurse }
$sums=[IO.File]::ReadAllLines("$assets\HIP\SHA256SUMS")
foreach($a in 'gfx1200','gfx1201'){
  Copy-Item "$stellar\$a\c32-wave1.hsaco" "$assets\HIP\$a\c32-wave1.hsaco" -Force
  $h=(Get-FileHash "$assets\HIP\$a\c32-wave1.hsaco").Hash.ToLower()
  $sums=$sums | ForEach-Object { if($_ -match "  $a/c32-wave1\.hsaco$"){"$h  $a/c32-wave1.hsaco"}else{$_} }
}
[IO.File]::WriteAllLines("$assets\HIP\SHA256SUMS",$sums,(New-Object Text.UTF8Encoding($false)))
Copy-Item "$lab\in\LmxxfNrRuntime.dll" "$lab\new\LmxxfNrRuntime.dll" -Force
Copy-Item "$lab\in\hip-re9-flags.txt" "$lab\new\DLSS5-AMD\native-game-flags.txt" -Force
New-Item -ItemType Directory -Force "$lab\old"|Out-Null
Copy-Item 'D:\DLSSNR-Lab\re9-fitlarge-20260923\LmxxfNrRuntime.dll' "$lab\old\LmxxfNrRuntime.dll" -Force
if(!(Test-Path "$lab\old\DLSS5-AMD\native-game-tiled-assets\HIP\SHA256SUMS")){ if(Test-Path "$lab\old\DLSS5-AMD"){cmd /c rmdir "$lab\old\DLSS5-AMD"}; Copy-Item "$lab\new\DLSS5-AMD" "$lab\old\DLSS5-AMD" -Recurse }
"new runtime " + (Get-FileHash "$lab\new\LmxxfNrRuntime.dll").Hash.Substring(0,12)
"old runtime " + (Get-FileHash "$lab\old\LmxxfNrRuntime.dll").Hash.Substring(0,12)
"modules gfx1201: " + @(Get-ChildItem "$assets\HIP\gfx1201" -Filter *.hsaco).Count + "  c32-wave1 " + (Get-FileHash "$assets\HIP\gfx1201\c32-wave1.hsaco").Hash.Substring(0,12)
"old sees shaders: " + (Test-Path "$lab\old\DLSS5-AMD\native-game-tiled-assets\native_codec_encode.hlsl")
