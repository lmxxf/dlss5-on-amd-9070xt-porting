param([ValidateSet('Control','Forced','Timing')][string]$Phase='Control',[int]$Period=4)
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend'
& "$r\vit-residual-run.ps1" -Phase $Phase -Period $Period -Tag '-gain' -Runner benchmark_vit_residual_gain.exe -CandidateModules "$r\vit-residual-gain-modules" -GainPath "$r\vit-residual-scales\gain.f32"
