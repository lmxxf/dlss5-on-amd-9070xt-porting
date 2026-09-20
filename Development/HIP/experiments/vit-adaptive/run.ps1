param([ValidateSet('Control','Quality','Timing')][string]$Phase='Control',[int]$Height=900,[int]$Sequence=0,[int]$Frames=12,[int]$Period=4,[double]$GlobalLimit=.22,[double]$LocalLimit=1.0,[string]$Tag='r1',[int]$Temporal=0,[int]$ResetEvery=0,[int]$CandidateMode=1,[int]$BaselineMode=0,[string]$CandidateModules='vit-residual-adaptive-modules',[string]$BaselineModules='vit-stream-exact-modules',[string]$CandidateRunner='benchmark_vit_adaptive.exe',[string]$BaselineRunner='benchmark_vit_adaptive.exe')
$ErrorActionPreference='Stop';$lab='D:\DLSSNR-Lab';$r="$lab\hip-backend";$root="$r\vit-adaptive-$Tag-results";$b="$lab\Magpie-DLSS5-AMD-0.23\DLSS5-AMD";New-Item -ItemType Directory -Force $root|Out-Null
function Idle {if(Get-Process re9,SB-Win64-Shipping,LOP-Win64-Shipping,Magpie -ErrorAction SilentlyContinue){throw 'Game/Magpie running'}}
function RunOne($height,$seq,$label,$mode,$frames,$diagnostic,$baseline){
 Idle;$d="$root\$label-$height-s$seq";New-Item -ItemType Directory -Force $d|Out-Null
 $log=if($diagnostic -and $mode){"$d\gate.csv"}else{''};if($log -and (Test-Path $log)){Remove-Item $log}
 $flags=@(Get-Content "$b\native-game-flags.txt")+@('DLSS5_HIP_MH_FEATURE_BYTE=1','DLSS5_HIP_MH_PROJ_DIAG_FB=1','DLSS5_HIP_MH_BYTE_STREAM=1','DLSS5_HIP_DECODER_BYTE=1','DLSS5_HIP_VIT_BYTE_STREAM=0','DLSS5_HIP_MH_FFN_FRAG256=1','DLSS5_HIP_GRAPH=0','DLSS5_SHOW_FPS=0','DLSS5_PRE_UPSCALE=0',"DLSS5_NETWORK_HEIGHT=$height","DLSS5_VIT_ADAPTIVE=$mode","DLSS5_VIT_REUSE_PERIOD=$(if($mode){$Period}else{0})","DLSS5_VIT_REUSE_GAIN=$r\vit-residual-scales\gain.f32","DLSS5_VIT_REUSE_GLOBAL=$GlobalLimit","DLSS5_VIT_REUSE_LOCAL=$LocalLimit","DLSS5_VIT_ADAPTIVE_LOG=$log","DLSS5_VIT_RESIDUAL_DUMP=","DLSS5_VIT_RESIDUAL_LOG=","DLSS5_RESIDUAL_SEQUENCE=$seq","DLSS5_RESIDUAL_RGB=$(if($diagnostic){1}else{0})")
 [IO.File]::WriteAllLines("$d\flags.txt",$flags)
 $modules=if($baseline){"$r\$BaselineModules"}else{"$r\$CandidateModules"};$runner=if($baseline){$BaselineRunner}else{$CandidateRunner}
 & "$r\$runner" "$b\native-game-tiled-assets" "$d\flags.txt" "$r\live-menu-before.f16" "$d\rgb" $frames $Temporal $modules $ResetEvery $(if($diagnostic){0}else{1}) 0 0 > "$d\run.log"
 if($LASTEXITCODE){throw "Run failed: $d"};Idle
 $rows=@(Import-Csv "$d\rgb.csv");if($rows.Count -ne $frames -or @($rows|Where-Object{[int]$_.invalid -ne 0}).Count){throw 'Missing/nonfinite output'}
 $warm=@($rows|Where-Object{[int]$_.frame -ge $(if($frames -ge 100){32}else{1})});$mean=($warm|Measure-Object wall_ms -Average).Average
 $row=[pscustomobject]@{height=$height;sequence=$seq;label=$label;mode=$mode;frames=$frames;diagnostic=$diagnostic;mean_ms=$mean;hash=(Get-FileHash "$d\rgb.f16").Hash}
 $row|Export-Csv "$d\summary.csv" -NoTypeInformation;$row|ConvertTo-Json -Compress
}
if($Phase -eq 'Control'){
 foreach($h in 900,1080){RunOne $h 0 'base' 0 12 $true $true
  foreach($mode in 0,2,3,1){RunOne $h 0 "control-$mode" $mode 12 $true $false
   if((Get-FileHash "$root\base-$h-s0\rgb.f16").Hash -ne (Get-FileHash "$root\control-$mode-$h-s0\rgb.f16").Hash){throw 'Static control differs'}
   if(@(Import-Csv "$root\control-$mode-$h-s0\rgb.csv"|Where-Object{[int]$_.changed_half_values_from_first -ne 0}).Count){throw 'Frozen output changed'}
  }
 }
}elseif($Phase -eq 'Quality'){
 RunOne $Height $Sequence 'base' $BaselineMode $Frames $true $true
 RunOne $Height $Sequence 'adaptive' $CandidateMode $Frames $true $false
}else{
 foreach($i in 0..3){$base=$i -in 0,3;RunOne $Height $Sequence "timing-$i" $(if($base){$BaselineMode}else{$CandidateMode}) 160 $false $base}
}
