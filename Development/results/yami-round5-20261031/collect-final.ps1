$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'wait for GPU jobs to end'}
foreach($c in @('mh-prob-gold','mh-prob-perf','c512-prob-long','c512-scope-gold','c512-scope-route-counts','c512-scope-verified-gold','c512-scope-compat','c512-scope-compat-rest','c512-scope-perf')){
 & "$r\collect.ps1" -Case $c
 Write-Output "COLLECTED $c"
}
