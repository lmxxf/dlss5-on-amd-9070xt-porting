$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend';$out="$r\int4-ffn-study\out"
function Idle {if(Get-Process SB-Win64-Shipping,Magpie,re9,LOP-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Game/Magpie running'}}
Idle;New-Item -ItemType Directory -Force $out|Out-Null
& "$r\int4-ffn-bench.exe" "$r\int4-gpu-data" "$r\vit-residual-adaptive-r3-modules\deep_fast-packed.hsaco" "$r\int4-ffn-study\int4-gfx1201.hsaco" $out
if($LASTEXITCODE){throw 'INT4 probe failed'};Idle
