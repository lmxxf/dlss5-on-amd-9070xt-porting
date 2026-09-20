$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend\sparse-ffn-study'
foreach($arch in 'gfx1200','gfx1201'){
 & 'D:\DLSSNR-Lab\dual-arch-src\rtc_compile.exe' "$r\$arch.hsaco" "$r\kernel.hip" comgr $arch
 if($LASTEXITCODE){throw 'Compile failed'}
 Get-FileHash "$r\$arch.hsaco"
}
