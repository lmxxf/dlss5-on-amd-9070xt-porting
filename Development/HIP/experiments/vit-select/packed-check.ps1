param([ValidateSet('Control','Sweep','Timing')][string]$Phase='Control',[string]$Threshold='0.5',[string]$Variant='packed',[string]$SweepThresholds='0,0.5,1')
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend'
& "$r\vit-select-run.ps1" -Phase $Phase -Threshold $Threshold -SweepThresholds $SweepThresholds -Tag $Variant -Runner benchmark_vit_packed.exe -BaselineModules "$r\vit-stream-exact-modules" -CandidateModules "$r\vit-$Variant-modules"
