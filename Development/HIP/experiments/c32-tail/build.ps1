# modules-prod = current production gfx1201 set (vit-proj-n64 set + c32-vec c32-wave1); + ctq-ctl / ctq-quad (c32-wave1 recipe + macros)
$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\hip-backend';$d=Split-Path -Parent $MyInvocation.MyCommand.Path
$m="$d\modules-prod";New-Item -ItemType Directory -Force $m | Out-Null
Copy-Item "$r\vit-proj-n64-production\modules-gfx1201\*.hsaco" $m
Copy-Item 'D:\DLSSNR-Lab\c32-vec-20260926\payload\gfx1201\c32-wave1.hsaco' "$m\c32-wave1.hsaco" -Force
"baseline c32-wave1 $((Get-FileHash "$m\c32-wave1.hsaco").Hash)"
foreach($n in 'ctq-ctl','ctq-quad'){
 & 'D:\DLSSNR-Lab\dual-arch-src\rtc_compile.exe' "$m\$n.hsaco" "$d\$n.hip" comgr gfx1201
 if($LASTEXITCODE){throw "Compile failed $n"}
 "$n $((Get-FileHash "$m\$n.hsaco").Hash)"
}
