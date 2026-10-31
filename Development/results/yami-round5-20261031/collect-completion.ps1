$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'wait for GPU jobs to end'}
$cases=@('final-panel-gold','final-c256-gold','c32-reduction-gold','c32-reduction-original-screen','c32-dotpar-perf','c128-one-plane-gold','c64-one-plane-gold','c256-one-plane-rc-gold','c512-prob-den-gold','c512-slot-gold','prob-asm-perf','pair-compat','minimal-gold')
foreach($c in $cases){& "$r\collect.ps1" -Case $c;Write-Output "COLLECTED $c"}
