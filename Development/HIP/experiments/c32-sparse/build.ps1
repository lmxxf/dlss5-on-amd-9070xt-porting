$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend';$d="$r\c32-sparse"
$s="#define HIP_ISA_HALF 1`n#define HIP_PREPACKED_WEIGHTS 1`n"+[IO.File]::ReadAllText("$d\c32_fused_ffn_attention.hip")
[IO.File]::WriteAllText("$d\packed.hip",$s)
foreach($arch in 'gfx1200','gfx1201'){
 New-Item -ItemType Directory -Force "$d\$arch"|Out-Null
 & 'D:\DLSSNR-Lab\dual-arch-src\rtc_compile.exe' "$d\$arch\c32_fused_ffn_attention-packed.hsaco" "$d\packed.hip" comgr $arch
 if($LASTEXITCODE){throw 'Compile failed'}
}
$m="$r\c32-sparse-modules";New-Item -ItemType Directory -Force $m|Out-Null
Copy-Item "$r\vit-residual-adaptive-r3-modules\*.hsaco" $m
Copy-Item "$d\gfx1201\*.hsaco" $m
