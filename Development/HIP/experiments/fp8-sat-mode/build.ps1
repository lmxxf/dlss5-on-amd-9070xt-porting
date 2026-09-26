# fp8-sat-mode: c32-wave1 with the production recipe, A = HIP_FP8_SAT_MODE 0 (must equal production code), B = 1.
# Baseline module set = the 0.32 modules installed in Stellar Blade; candidate = same set with c32-wave1 replaced.
param([string]$Arch='gfx1201',[int[]]$Variants=@(0,1))
$ErrorActionPreference='Stop';$d=Split-Path -Parent $MyInvocation.MyCommand.Path
$rtc='D:\DLSSNR-Lab\dual-arch-src\rtc_compile.exe';$src="$d\hip"
$sb="C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64\DLSS5-AMD\native-game-tiled-assets\HIP\$Arch"
$utf8=New-Object Text.UTF8Encoding($false)
foreach($v in $Variants){
 $m="$d\modules-$v-$Arch";New-Item -ItemType Directory -Force $m|Out-Null;Copy-Item "$sb\*.hsaco" $m
 $t="#define HIP_ISA_HALF 1`n"+(('HIP_PREPACKED_WEIGHTS 1','CW_ROLL_HIDDEN 1','CW_ROLL_WINDOW 1','CW_VEC_INPUT 1','CW_PREFIX_SPLIT 1',"HIP_FP8_SAT_MODE $v")|ForEach-Object{"#define $_`n"}) -join ''
 foreach($p in 'c32_fused_ffn_attention.hip','wave_owned_c32.inc'){$t+=[IO.File]::ReadAllText("$src\$p")+"`n"}
 [IO.File]::WriteAllText("$d\c32-wave1-$v.generated.hip",$t,$utf8)
 & $rtc "$m\c32-wave1.hsaco" "$d\c32-wave1-$v.generated.hip" comgr $Arch|Out-Null;if($LASTEXITCODE){throw "compile $v"}
 Copy-Item "$m\c32-wave1.hsaco.s" "$d\c32-wave1-$v.s"
 "{0} c32-wave1 SAT_MODE={1} {2}" -f $Arch,$v,(Get-FileHash "$m\c32-wave1.hsaco").Hash
}
