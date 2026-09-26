# c32-tail: candidates (SWEEP_CANDS) on production prod8+wave-owned+PDL+C512_M32+VIT_PROJ_N64; modules-<Modules> = production + cvi-*.hsaco.
param([int[]]$Heights=@(900,1080),[int]$Frames=150,[int]$Repeats=2,[string]$Label='census',[int]$Pdl=1,[string]$Targets='',[string]$Cands='',[string]$Modules='prod')
$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\hip-backend';$d=Split-Path -Parent $MyInvocation.MyCommand.Path;$a='D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.23\DLSS5-AMD';$cap="$r\network-timeline"
function Idle{$p=Get-Process LOP-Win64-Shipping,SB-Win64-Shipping,Cyberpunk2077,OnimushaWotS,re9,Magpie -ErrorAction SilentlyContinue;if($p){throw "Game running: $($p.ProcessName -join ',')"}}
$m="$d\modules-$Modules"
if(!(Test-Path "$m\ctq-quad.hsaco")){throw 'run build.ps1 first'}
foreach($height in $Heights){
 Idle;$f="$d\results-$height-$Label";New-Item -ItemType Directory -Force $f|Out-Null
 $flags=@(Get-Content "$a\native-game-flags.txt")+@('DLSS5_HIP_MH_FEATURE_BYTE=1','DLSS5_HIP_MH_PROJ_DIAG_FB=1','DLSS5_HIP_MH_BYTE_STREAM=1','DLSS5_HIP_DECODER_BYTE=1','DLSS5_HIP_VIT_BYTE_STREAM=0','DLSS5_HIP_MH_FFN_FRAG256=1','DLSS5_HIP_GRAPH=0','DLSS5_VIT_ADAPTIVE=0',"DLSS5_NETWORK_HEIGHT=$height","DLSS5_HIP_PDL=$Pdl",'DLSS5_HIP_WAVE_OWNED=1','DLSS5_HIP_C512_M32=1','DLSS5_HIP_VIT_PROJ_N64=1')
 [IO.File]::WriteAllLines("$f\flags.txt",$flags)
 $w=if($height -eq 900){1600}else{1920};$h=if($height -eq 900){960}else{1152}
 Push-Location $f;$env:SWEEP_TARGETS=$Targets;$env:SWEEP_CANDS=$Cands
 try{& "$d\network.exe" "$a\native-game-tiled-assets" $m "$f\flags.txt" "$cap\$height\input.f32" "$cap\$height\expected.f32" $w $h $Frames $Repeats > run.log 2>&1;$code=$LASTEXITCODE;Get-Content run.log|Select-String 'CONFIG|VERIFY|PASS|FAIL'|Select-Object -Last 6;if($code){throw "ctq failed $height $Label"}}finally{Pop-Location}
}
