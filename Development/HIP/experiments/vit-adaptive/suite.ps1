param([ValidateSet('Quality','Timing')][string]$Phase='Quality',[string]$Tag='r1')
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend'
if($Phase -eq 'Quality'){foreach($seq in 1..7){& "$r\vit-adaptive-run.ps1" -Phase Quality -Height 900 -Sequence $seq -Tag $Tag}}
else{foreach($h in 900,1080){foreach($seq in 0,1,5){& "$r\vit-adaptive-run.ps1" -Phase Timing -Height $h -Sequence $seq -Tag $Tag}}}
