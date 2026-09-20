$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend';$out="$r\int4-ffn-study";New-Item -ItemType Directory -Force $out|Out-Null
foreach($arch in 'gfx1200','gfx1201'){
 & 'D:\DLSSNR-Lab\dual-arch-src\rtc_compile.exe' "$out\int4-$arch.hsaco" "$out\int4.hip" comgr $arch
 if($LASTEXITCODE){throw "Compile failed $arch"}
 Get-FileHash "$out\int4-$arch.hsaco"
}
