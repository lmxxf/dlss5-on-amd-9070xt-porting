param([ValidateSet('Control','Sweep','Timing')][string]$Phase='Control',[string]$Threshold='0.5',[string]$Duplicate='',[string]$Tag='stream')
$ErrorActionPreference='Stop';$lab='D:\DLSSNR-Lab';$r="$lab\hip-backend";$work="$r\vit-select-$Tag-results";$b="$lab\Magpie-DLSS5-AMD-0.23\DLSS5-AMD";New-Item -ItemType Directory -Force $work|Out-Null
function Idle {if(Get-Process re9,SB-Win64-Shipping,LOP-Win64-Shipping,Magpie -ErrorAction SilentlyContinue){throw 'Game/Magpie running'}}
$flags=@(Get-Content "$b\native-game-flags.txt")+@('DLSS5_HIP_MH_FEATURE_BYTE=1','DLSS5_HIP_MH_PROJ_DIAG_FB=1','DLSS5_HIP_MH_BYTE_STREAM=1','DLSS5_HIP_DECODER_BYTE=1','DLSS5_HIP_VIT_BYTE_STREAM=0','DLSS5_HIP_MH_FFN_FRAG256=1','DLSS5_HIP_GRAPH=0','DLSS5_SHOW_FPS=0','DLSS5_PRE_UPSCALE=0',"DLSS5_HIP_DUP_PREFIX=$Duplicate",'DLSS5_HIP_DUP_COUNT=2')
function RunOne($height,$label,$threshold,$pattern,$frames,$diag,$base){
 Idle;$m=if($base){"$lab\c256-frag-production-modules\gfx1201"}else{"$r\vit-select-modules"};$prefix="$work\$label-$height-p$pattern";$f="$work\flags.txt"
 $route=if($diag){"$prefix-route.csv"}else{''};if($diag -and (Test-Path $route)){Remove-Item $route}
 [IO.File]::WriteAllLines($f,($flags+@("DLSS5_NETWORK_HEIGHT=$height","DLSS5_VIT_SELECT_THRESHOLD=$threshold","DLSS5_VIT_SELECT_DIAG=$route")))
 & "$r\benchmark_vit_select.exe" "$b\native-game-tiled-assets" $f "$r\live-menu-before.f16" $prefix $frames 0 $m 0 $(if($frames -ge 100){1}else{0}) 0 $pattern > "$prefix.log"
 if($LASTEXITCODE){throw "Replay failed $label $height"};Idle
 $rows=@(Import-Csv "$prefix.csv");if($rows.Count -ne $frames -or @($rows|Where-Object{[int]$_.invalid -ne 0}).Count){throw 'Bad frame count/finite'}
 $t=@($rows|Where-Object{[int]$_.frame -ge $(if($frames -ge 100){32}else{1})}|ForEach-Object{[double]$_.wall_ms}|Sort-Object);$median=($t[[int][Math]::Floor(($t.Count-1)/2)]+$t[[int][Math]::Floor($t.Count/2)])/2
 $row=[pscustomobject]@{height=$height;label=$label;threshold=$threshold;pattern=$pattern;frames=$frames;median_ms=$median;hash=(Get-FileHash "$prefix.f16").Hash};$row|ConvertTo-Json -Compress
 $row|Export-Csv "$prefix-summary.csv" -NoTypeInformation
}
if($Phase -eq 'Control'){
 foreach($h in 900,1080){foreach($p in 0,1,2){RunOne $h 'base' '' $p 6 $false $true;RunOne $h 'exact' '-1' $p 6 $true $false
 if((Get-FileHash "$work\base-$h-p$p.f16").Hash -ne (Get-FileHash "$work\exact-$h-p$p.f16").Hash){throw 'All-exact control differs'}
 }}
}elseif($Phase -eq 'Sweep'){
 foreach($h in 900,1080){foreach($threshold in '0','0.5','1') {foreach($p in 0,1,2){RunOne $h "select-$threshold" $threshold $p 6 $true $false}}}
}else{
 foreach($h in 900,1080){foreach($i in 0..3){if($i -in 0,3){RunOne $h "timing-$Threshold-$Duplicate-base-$i" '' 0 160 $false $true}else{RunOne $h "timing-$Threshold-$Duplicate-select-$i" $Threshold 0 160 $false $false}}}
}
