$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend'
& "$r\packed-check.ps1" -Variant fragment -Phase Control
& "$r\packed-check.ps1" -Variant fragment -Phase Sweep
foreach($h in 900,1080){foreach($t in '0','0.5','1'){foreach($p in 0,1,2){
 if((Get-FileHash "$r\vit-select-fragment-results\select-$t-$h-p$p.f16").Hash -ne (Get-FileHash "$r\vit-select-packed-results\select-$t-$h-p$p.f16").Hash){throw 'Fragment layout changed approximate output'}
}}}
'FRAGMENT: all exact controls and 18 approximate outputs match'
foreach($t in '-1','0.5','1'){& "$r\packed-check.ps1" -Variant fragment -Phase Timing -Threshold $t}
