param([ValidateSet('Control','Sweep','Timing')][string]$Phase='Control',[string]$Threshold='0.5',[string]$Variant='packed')
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend'
& "$r\run.ps1" -Phase $Phase -Threshold $Threshold -Tag $Variant -Runner benchmark_vit_packed.exe -BaselineModules "$r\vit-stream-exact-modules" -CandidateModules "$r\vit-$Variant-modules"
