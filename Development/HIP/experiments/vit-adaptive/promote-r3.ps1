# Promote lab artifacts only; this does NOT install into a game.
$ErrorActionPreference='Stop';$lab='D:\DLSSNR-Lab';$r="$lab\hip-backend"
if(!(Test-Path "$lab\vit-adaptive-r2-frozen-build")){Copy-Item "$lab\vit-residual-adaptive-build" "$lab\vit-adaptive-r2-frozen-build" -Recurse}
if(!(Test-Path "$r\native-vit-adaptive-r2.addon64")){Copy-Item "$r\native-vit-adaptive.addon64" "$r\native-vit-adaptive-r2.addon64"}
$expected='FD451EFB4AA566795F17CA6D9E1B2166F4C99F68E9E5FD6E63B75A0590CEF696'
if((Get-FileHash "$r\native-vit-adaptive-r3.addon64").Hash -ne $expected){throw 'R3 DLL mismatch'}
foreach($arch in 'gfx1200','gfx1201'){Copy-Item "$lab\vit-residual-adaptive-r3-build\$arch\*" "$lab\vit-residual-adaptive-build\$arch" -Force}
Copy-Item "$r\vit-residual-adaptive-r3-modules\*.hsaco" "$r\vit-residual-adaptive-modules" -Force
Copy-Item "$r\benchmark_vit_adaptive_r3.exe" "$r\benchmark_vit_adaptive.exe" -Force
Copy-Item "$r\native-vit-adaptive-r3.addon64" "$r\native-vit-adaptive.addon64" -Force
& "$r\stage-adaptive-preview.ps1"
