param([ValidateSet('Smoke','Timing')][string]$Phase='Smoke')
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend'
$opts=@{CandidateModules='vit-residual-adaptive-r3-modules';CandidateRunner='benchmark_vit_image_overlap.exe';BaselineModules='vit-residual-adaptive-r3-modules';BaselineRunner='benchmark_vit_adaptive_r3.exe';BaselineMode=1}
foreach($kind in 1,2){
 if($Phase -eq 'Smoke'){
  foreach($seq in 0,1,6){$tag="image-schedule-$kind-smoke";& "$r\vit-adaptive-run.ps1" @opts -Phase Quality -Sequence $seq -Tag $tag -ImageSchedule $kind
   $work="$r\vit-adaptive-$tag-results";foreach($i in 0..11){if((Get-FileHash "$work\base-900-s$seq\rgb-frame-$i.f16").Hash -ne (Get-FileHash "$work\adaptive-900-s$seq\rgb-frame-$i.f16").Hash){throw "Image schedule differs kind=$kind seq=$seq frame=$i"}}
   Write-Output "EXACT_MATCH kind=$kind sequence=$seq frames=12"
  }
 }else{foreach($h in 900,1080){& "$r\vit-adaptive-run.ps1" @opts -Phase Timing -Height $h -Sequence 1 -Temporal 1 -Tag "image-schedule-$kind-timing" -ImageSchedule $kind}}
}
