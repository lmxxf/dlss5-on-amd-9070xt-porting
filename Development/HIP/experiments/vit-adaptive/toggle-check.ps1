$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend';$b='D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.23\DLSS5-AMD';$d="$r\vit-adaptive-r2-toggle-results"
if(Get-Process re9,SB-Win64-Shipping,LOP-Win64-Shipping,Magpie -ErrorAction SilentlyContinue){throw 'Game/Magpie running'}
New-Item -ItemType Directory -Force $d|Out-Null
$flags=@(Get-Content "$r\vit-adaptive-r2-results\adaptive-900-s2\flags.txt")+@('DLSS5_VIT_ADAPTIVE_LOG=')
[IO.File]::WriteAllLines("$d\flags.txt",$flags)
& "$r\benchmark_vit_toggle.exe" "$b\native-game-tiled-assets" "$d\flags.txt" "$r\live-menu-before.f16" "$d\rgb" 12 0 "$r\vit-residual-adaptive-modules" 0 0 0 0 > "$d\run.log"
if($LASTEXITCODE){throw 'Toggle replay failed'}
$rows=@(Import-Csv "$d\rgb.csv");if($rows.Count -ne 12 -or @($rows|Where-Object{[int]$_.invalid -ne 0}).Count){throw 'Nonfinite/missing toggle output'}
foreach($i in 0,4,5,6,7,8){if((Get-FileHash "$d\rgb-frame-$i.f16").Hash -ne (Get-FileHash "$r\vit-adaptive-r2-results\base-900-s2\rgb-frame-$i.f16").Hash){throw "Toggle exact/reenable frame differs: $i"}}
'PASS: mode disabled frames4..7 is exact with period4 still set; reenable8 refreshes anchor.'|Tee-Object "$d\result.txt"
