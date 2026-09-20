param([ValidateSet('Control','Capture','Forced','Timing')][string]$Phase='Control',[int]$Height=900,[int]$Period=4,[string]$Tag="",[string]$GainPath="",[string]$Runner="benchmark_vit_residual.exe",[string]$CandidateModules="")
$ErrorActionPreference='Stop';$lab='D:\DLSSNR-Lab';$r="$lab\hip-backend";$work="$r\vit-residual$Tag-results";$b="$lab\Magpie-DLSS5-AMD-0.23\DLSS5-AMD";New-Item -ItemType Directory -Force $work|Out-Null
function Idle {if(Get-Process re9,SB-Win64-Shipping,LOP-Win64-Shipping,Magpie -ErrorAction SilentlyContinue){throw 'Game/Magpie running'}}
$flags=@(Get-Content "$b\native-game-flags.txt")+@('DLSS5_HIP_MH_FEATURE_BYTE=1','DLSS5_HIP_MH_PROJ_DIAG_FB=1','DLSS5_HIP_MH_BYTE_STREAM=1','DLSS5_HIP_DECODER_BYTE=1','DLSS5_HIP_VIT_BYTE_STREAM=0','DLSS5_HIP_MH_FFN_FRAG256=1','DLSS5_HIP_GRAPH=0','DLSS5_SHOW_FPS=0','DLSS5_PRE_UPSCALE=0',"DLSS5_VIT_REUSE_GAIN=$GainPath")
function RunOne($h,$label,$period,$sequence,$frames,$capture,$base){
 Idle;$dir="$work\$label-$h-s$sequence";New-Item -ItemType Directory -Force $dir|Out-Null;$dump=if($capture){"$dir\features"}else{''};if($capture){New-Item -ItemType Directory -Force $dump|Out-Null}
 $log=if($capture){"$dir\decisions.csv"}else{''};if($log -and (Test-Path $log)){Remove-Item $log}
 $f="$dir\flags.txt";[IO.File]::WriteAllLines($f,($flags+@("DLSS5_NETWORK_HEIGHT=$h","DLSS5_VIT_REUSE_PERIOD=$period","DLSS5_VIT_RESIDUAL_DUMP=$dump","DLSS5_VIT_RESIDUAL_LOG=$log","DLSS5_RESIDUAL_SEQUENCE=$sequence","DLSS5_RESIDUAL_RGB=$(if($capture){1}else{0})")))
 $m=if($base){"$r\vit-stream-exact-modules"}else{if($CandidateModules){$CandidateModules}else{"$r\vit-residual-modules"}}
 & "$r\$Runner" "$b\native-game-tiled-assets" $f "$r\live-menu-before.f16" "$dir\rgb" $frames 0 $m 0 $(if($frames -ge 100){1}else{0}) 0 0 > "$dir\run.log"
 if($LASTEXITCODE){throw "Residual replay failed $label"};Idle
 $rows=@(Import-Csv "$dir\rgb.csv");if($rows.Count -ne $frames -or @($rows|Where-Object{[int]$_.invalid -ne 0}).Count){throw 'Invalid/missing output'}
 $t=@($rows|Where-Object{[int]$_.frame -ge $(if($frames -ge 100){32}else{1})}|ForEach-Object{[double]$_.wall_ms}|Sort-Object);$median=($t[[int][Math]::Floor(($t.Count-1)/2)]+$t[[int][Math]::Floor($t.Count/2)])/2
 $warm=@($rows|Where-Object{[int]$_.frame -ge $(if($frames -ge 100){32}else{1})});$mean=($warm|Measure-Object wall_ms -Average).Average
 $full=@($warm|Where-Object{$period -le 1 -or ([int]$_.frame % $period) -eq 0});$reused=@($warm|Where-Object{$period -gt 1 -and ([int]$_.frame % $period) -ne 0})
 $row=[pscustomobject]@{height=$h;label=$label;period=$period;sequence=$sequence;frames=$frames;capture=$capture;mean_ms=$mean;median_ms=$median;full_frames=$full.Count;reuse_frames=$reused.Count;full_mean_ms=($full|Measure-Object wall_ms -Average).Average;reuse_mean_ms=($reused|Measure-Object wall_ms -Average).Average;hash=(Get-FileHash "$dir\rgb.f16").Hash};$row|ConvertTo-Json -Compress;$row|Export-Csv "$dir\summary.csv" -NoTypeInformation
}
if($Phase -eq 'Control'){
 foreach($h in 900,1080){RunOne $h 'base' 0 0 12 $false $true;foreach($p in 0,2,4){RunOne $h "control-$p" $p 0 12 $false $false
 if((Get-FileHash "$work\base-$h-s0\rgb.f16").Hash -ne (Get-FileHash "$work\control-$p-$h-s0\rgb.f16").Hash){throw 'Static reuse control differs'}
 $rows=Import-Csv "$work\control-$p-$h-s0\rgb.csv";if(@($rows|Where-Object{[int]$_.changed_half_values_from_first -ne 0}).Count){throw 'Static output changed'}
 }}
}elseif($Phase -eq 'Capture'){
 foreach($seq in 0..4){RunOne $Height 'capture' 0 $seq 12 $true $false}
}elseif($Phase -eq 'Forced'){
 foreach($seq in 1..4){RunOne $Height "forced-$Period" $Period $seq 12 $true $false}
}else{
 foreach($h in 900,1080){foreach($i in 0..3){if($i -in 0,3){RunOne $h "timing-$Period-base-$i" 0 0 160 $false $true}else{RunOne $h "timing-$Period-reuse-$i" $Period 0 160 $false $false}}}
}
