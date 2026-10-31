$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'wait for GPU jobs to end'}
$cases=@('r2d-gold','vit-fix-gold','c512-den-gold','prob-gold','vit-rtz-gold','fn-cache-gold','token-pair-gold','pair-select-gold','pair-bind-gold','pair-qualified-gold','pair-compat-flags','deep-four-gold','rtz-exhaustive-run','occupancy','layout-gold','c512-prob-gold','prob-asm-gold','prob-asm-verified-gold','c512-prob-perf','prob-asm-verified-perf','r2d-quiet-confirm')
foreach($c in $cases){& "$r\collect.ps1" -Case $c}
