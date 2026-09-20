param([ValidateSet('packed','fragment','spatial')][string]$Variant='packed')
$ErrorActionPreference='Stop';$lab='D:\DLSSNR-Lab';$r="$lab\hip-backend";$src="$lab\vit-$Variant-src"
New-Item -ItemType Directory -Force $src|Out-Null;Copy-Item "$r\vit-$Variant.hip" "$src\deep_fast.hip" -Force
& "$r\build-vIT-experiment.ps1" -SourceDir $src -OutputDir "$lab\vit-$Variant-build" -Compiler "$lab\dual-arch-src\rtc_compile.exe" -Only deep_fast-packed
$c="$r\vit-$Variant-modules";New-Item -ItemType Directory -Force $c|Out-Null;Copy-Item "$lab\c256-frag-production-modules\gfx1201\*.hsaco" $c -Force;Copy-Item "$lab\vit-$Variant-build\gfx1201\deep_fast-packed.hsaco" $c -Force
