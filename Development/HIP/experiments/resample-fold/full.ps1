# Bit-exactness (7 cases x EXACT/AE x 12 frames + AE decision CSV + 900/1080 history ticket rollover) and two-round ABBA
# base = benchmark-base.exe (installed host 62803606 source) + flat-A; candidate = benchmark-<Host>.exe + flat-<Set>.
param([string]$BaseSet='A',[string]$BaseBench='benchmark-base.exe',[string]$Set='S',[string]$Cand='P',[string]$RollHost='Proll',[switch]$SkipCorrect,[switch]$SkipTiming,[int]$Rounds=2)
$ErrorActionPreference='Stop'
$root='D:\DLSSNR-Lab\hip-backend\resample-fold-20261001'
$env:SP_CHANNELS='4';$env:SP_SIDES='3';$env:SP_FORCE_TIMEOUT='0';$env:SP_VALIDATE='0';$env:SP_TRACE='0';$env:SP_TICKET_LIMIT='4294967295';$env:SP_TICKET_START='0'
$cb="benchmark-$Cand.exe";if($Cand -eq 'base'){$cb='benchmark-base.exe'}
$rb="benchmark-$RollHost.exe";if($RollHost -eq 'base'){$rb='benchmark-base.exe'}
if(!$SkipCorrect){
 foreach($ae in 0,1){
  $batch=if($ae){"adaptive-$Cand"}else{"correct-$Cand"}
  & "$root\regression.ps1" -Set $Set -Adaptive $ae -BenchName $BaseBench -Base $BaseSet -CandidateBenchName $cb -CorrectnessOnly -Batch $batch
  if(!$?){throw 'Full regression failed'}
 }
 foreach($slot in Get-ChildItem "$root\runtime-regression-$Set-adaptive-$Cand" -Directory -Filter '*-True'){
  $base=$slot.FullName.Substring(0,$slot.FullName.Length-4)+'False'
  if([IO.File]::ReadAllText("$base\adaptive.csv") -cne [IO.File]::ReadAllText("$($slot.FullName)\adaptive.csv")){throw 'AE decisions changed'}
 };"AE CSV SAME $Set"
 $env:SP_VALIDATE='1';$env:SP_TRACE='1';$env:SP_TICKET_LIMIT='1024';$env:SP_TICKET_START='4294967290'
 foreach($ae in 0,1){
  & "$root\regression.ps1" -Set $Set -Adaptive $ae -BenchName $BaseBench -Base $BaseSet -CandidateBenchName $rb -CorrectnessOnly -Only @('900-history','1080-history') -Batch "roll-$RollHost-$ae"
  if(!$?){throw 'Rollover regression failed'}
 }
 $env:SP_VALIDATE='0';$env:SP_TRACE='0';$env:SP_TICKET_LIMIT='4294967295';$env:SP_TICKET_START='0'
}
if(!$SkipTiming){foreach($round in 1..$Rounds){
 & "$root\regression.ps1" -Set $Set -BenchName $BaseBench -Base $BaseSet -CandidateBenchName $cb -TimingOnly -TimingFrames 1000 -Batch "timing-$Cand-$round"
 if(!$?){throw 'ABBA failed'}
}}
"FULL_DONE $Set $Cand"
