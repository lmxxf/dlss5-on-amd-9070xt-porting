param([switch]$Timing,[switch]$Build)
$ErrorActionPreference='Stop';$lab='D:\DLSSNR-Lab';$r="$lab\hip-backend";$work="$r\vit-stream-exact-results";$b="$lab\Magpie-DLSS5-AMD-0.23\DLSS5-AMD";New-Item -ItemType Directory -Force $work|Out-Null
function Idle {if(Get-Process re9,SB-Win64-Shipping,LOP-Win64-Shipping,Magpie -ErrorAction SilentlyContinue){throw 'Game/Magpie running'}}
Idle
if($Build){$src="$lab\vit-stream-exact-src";New-Item -ItemType Directory -Force $src|Out-Null;Copy-Item "$r\vit-stream-exact.hip" "$src\deep_fast.hip" -Force;& "$r\build-vIT-experiment.ps1" -SourceDir $src -OutputDir "$lab\vit-stream-exact-build" -Compiler "$lab\dual-arch-src\rtc_compile.exe" -Only deep_fast-packed
$c="$r\vit-stream-exact-modules";New-Item -ItemType Directory -Force $c|Out-Null;Copy-Item "$lab\c256-frag-production-modules\gfx1201\*.hsaco" $c -Force;Copy-Item "$lab\vit-stream-exact-build\gfx1201\deep_fast-packed.hsaco" $c -Force}
$flags=@(Get-Content "$b\native-game-flags.txt")+@('DLSS5_HIP_MH_FEATURE_BYTE=1','DLSS5_HIP_MH_PROJ_DIAG_FB=1','DLSS5_HIP_MH_BYTE_STREAM=1','DLSS5_HIP_DECODER_BYTE=1','DLSS5_HIP_VIT_BYTE_STREAM=0','DLSS5_HIP_MH_FFN_FRAG256=1','DLSS5_HIP_GRAPH=0','DLSS5_SHOW_FPS=0','DLSS5_PRE_UPSCALE=0')
$heights=if($Timing){@(900,1080)}else{@(720,900,1080)};$patterns=if($Timing){@(0)}else{@(0,1,2)};$iterations=if($Timing){@(0,1,2,3)}else{@(0,1)};$frames=if($Timing){160}else{12}
foreach($h in $heights){foreach($p in $patterns){$hashes=@();foreach($i in $iterations){
 Idle;$candidate=$i -in 1,2;$m=if($candidate){"$r\vit-stream-exact-modules"}else{"$lab\c256-frag-production-modules\gfx1201"};$prefix="$work\$(if($Timing){'timing'}else{'quality'})-$h-p$p-$i";$f="$work\flags.txt";[IO.File]::WriteAllLines($f,($flags+@("DLSS5_NETWORK_HEIGHT=$h")))
 & "$r\benchmark_c256frag_production.exe" "$b\native-game-tiled-assets" $f "$r\live-menu-before.f16" $prefix $frames 0 $m 0 $(if($Timing){1}else{0}) 0 $p > "$prefix.log"
 if($LASTEXITCODE){throw 'Exact stream replay failed'};Idle
 $rows=@(Import-Csv "$prefix.csv");if($rows.Count -ne $frames -or @($rows|Where-Object{[int]$_.invalid -ne 0}).Count){throw 'Frame validation failed'}
 $t=@($rows|Where-Object{[int]$_.frame -ge $(if($Timing){32}else{1})}|ForEach-Object{[double]$_.wall_ms}|Sort-Object);$median=($t[[int][Math]::Floor(($t.Count-1)/2)]+$t[[int][Math]::Floor($t.Count/2)])/2
 $hash=(Get-FileHash "$prefix.f16").Hash;$hashes+=$hash;$row=[pscustomobject]@{height=$h;pattern=$p;iteration=$i;candidate=$candidate;frames=$frames;median_ms=$median;hash=$hash};$row|ConvertTo-Json -Compress;$row|Export-Csv "$prefix-summary.csv" -NoTypeInformation
};if(@($hashes|Select-Object -Unique).Count -ne 1){throw 'Exact stream output mismatch'}}}
