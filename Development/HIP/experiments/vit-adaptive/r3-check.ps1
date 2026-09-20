param([ValidateSet('Control','Quality','Compare','Timing')][string]$Phase='Control')
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend'
$args=@{CandidateModules='vit-residual-adaptive-r3-modules';CandidateRunner='benchmark_vit_adaptive_r3.exe'}
if($Phase -eq 'Control'){& "$r\vit-adaptive-run.ps1" @args -Phase Control -Tag r3-control}
elseif($Phase -eq 'Quality'){foreach($s in 1..7){& "$r\vit-adaptive-run.ps1" @args -Phase Quality -Sequence $s -Tag r3-quality}}
else{
 if($Phase -eq 'Compare'){$args.BaselineModules='vit-adaptive-r2-frozen-modules';$args.BaselineRunner='benchmark_vit_adaptive_r2.exe';$args.BaselineMode=1}
 foreach($h in 900,1080){foreach($t in 0,1){$seq=if($t){1}else{0};$tag="r3-$($Phase.ToLower())-h$t";& "$r\vit-adaptive-run.ps1" @args -Phase Timing -Height $h -Sequence $seq -Temporal $t -Tag $tag}}
}
