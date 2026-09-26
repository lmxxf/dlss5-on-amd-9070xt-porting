# c64-block-fused / W2_PACK8: c64-wave2 with the production recipe (hip/build-modules.ps1), A = W2_PACK8 0 (must equal the
# 0.32 module's code sections), B = W2_PACK8 1. Module set = the 0.32 set installed in Stellar Blade with c64-wave2 replaced.
param([string]$Arch='gfx1201',[int[]]$Variants=@(0,1))
$ErrorActionPreference='Stop';$d=Split-Path -Parent $MyInvocation.MyCommand.Path
$rtc='D:\DLSSNR-Lab\dual-arch-src\rtc_compile.exe';$src="$d\hip"
$sb="C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64\DLSS5-AMD\native-game-tiled-assets\HIP\$Arch"
$utf8=New-Object Text.UTF8Encoding($false)
$defs='HIP_PREPACKED_WEIGHTS 1','HIP_FFN_HOIST_RES 2','HIP_PDL_KERNELS 0','W2_FRAGMENT_WEIGHTS 1','W2_LAUNDER_QKV 1','W2_SCHED_FENCE 1','W2_ROLL_QUERY 1','W2_HIDDEN_TILES 2'
foreach($v in $Variants){
 $m="$d\modules-$v-$Arch";New-Item -ItemType Directory -Force $m|Out-Null;Copy-Item "$sb\*.hsaco" $m
 $t="#define HIP_ISA_HALF 1`n"+((@($defs)+"W2_PACK8 $v")|ForEach-Object{"#define $_`n"}) -join ''
 $core=[IO.File]::ReadAllText("$src\wave_owned_mh.inc");$s=$core.IndexOf(' // One wave owns all keys');$e=$core.IndexOf('#define W2_KERNEL')
 foreach($p in 'multihead_fast_padded.hip','wave_owned_mh.inc','wave_owned_attention_setup.inc','@body','wave_owned_attention_exports.inc'){
  if($p -eq '@body'){$t+=$core.Substring($s,$e-$s)+"`n"}else{$t+=[IO.File]::ReadAllText("$src\$p")+"`n"}}
 [IO.File]::WriteAllText("$d\c64-wave2-$v.generated.hip",$t,$utf8)
 & $rtc "$m\c64-wave2.hsaco" "$d\c64-wave2-$v.generated.hip" comgr $Arch|Out-Null;if($LASTEXITCODE){throw "compile $v"}
 Copy-Item "$m\c64-wave2.hsaco.s" "$d\c64-wave2-$v-$Arch.s"
 "{0} c64-wave2 W2_PACK8={1} {2}" -f $Arch,$v,(Get-FileHash "$m\c64-wave2.hsaco").Hash
}
