$ErrorActionPreference='Stop';$lab='D:\DLSSNR-Lab';$r="$lab\hip-backend";$src="$lab\vit-residual-src"
New-Item -ItemType Directory -Force $src|Out-Null;Copy-Item "$r\vit-residual.hip" "$src\deep_fast.hip" -Force
& "$r\build-vIT-experiment.ps1" -SourceDir $src -OutputDir "$lab\vit-residual-build" -Compiler "$lab\dual-arch-src\rtc_compile.exe" -Only deep_fast-packed
$c="$r\vit-residual-modules";New-Item -ItemType Directory -Force $c|Out-Null;Copy-Item "$r\vit-stream-exact-modules\*.hsaco" $c -Force;Copy-Item "$lab\vit-residual-build\gfx1201\deep_fast-packed.hsaco" $c -Force
