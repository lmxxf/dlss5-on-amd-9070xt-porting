param([ValidateSet('Check','Timing')][string]$Phase='Check')
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend';$d="$r\c32-contract-shuffle-results";$b='D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.23\DLSS5-AMD'
function Idle {if(Get-Process SB-Win64-Shipping,Magpie,re9,LOP-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Game/Magpie running'}}
function One($tag,$mode,$old,$frames){
 Idle;New-Item -ItemType Directory -Force "$d\$tag"|Out-Null
 $env:DLSS5_C32_SPARSE="$mode"
 $flags=@(Get-Content "$b\native-game-flags.txt")+@('DLSS5_HIP_MH_FEATURE_BYTE=1','DLSS5_HIP_MH_PROJ_DIAG_FB=1','DLSS5_HIP_MH_BYTE_STREAM=1','DLSS5_HIP_DECODER_BYTE=1','DLSS5_HIP_VIT_BYTE_STREAM=0','DLSS5_HIP_MH_FFN_FRAG256=1','DLSS5_HIP_GRAPH=0','DLSS5_SHOW_FPS=0','DLSS5_PRE_UPSCALE=0','DLSS5_NETWORK_HEIGHT=900','DLSS5_VIT_ADAPTIVE=0','DLSS5_VIT_REUSE_PERIOD=0','DLSS5_RESIDUAL_SEQUENCE=0',"DLSS5_C32_SPARSE=$mode")
 [IO.File]::WriteAllLines("$d\$tag\flags.txt",$flags)
 $m=if($old){"$r\vit-residual-adaptive-r3-modules"}else{"$r\c32-sparse-modules"}
 & "$r\benchmark_c32_sparse.exe" "$b\native-game-tiled-assets" "$d\$tag\flags.txt" "$r\live-menu-before.f16" "$d\$tag\rgb" $frames 0 $m 0 1 0 0 > "$d\$tag\run.log"
 if($LASTEXITCODE){throw "run failed $tag"};Idle
 $rows=@(Import-Csv "$d\$tag\rgb.csv");if(@($rows|Where-Object{[int]$_.invalid -ne 0}).Count){throw 'Nonfinite'}
 [pscustomobject]@{tag=$tag;mean=($rows|Where-Object{[int]$_.frame -ge $(if($frames -gt 100){32}else{1})}|Measure-Object wall_ms -Average).Average;hash=(Get-FileHash "$d\$tag\rgb.f16").Hash}|ConvertTo-Json -Compress
}
if($Phase -eq 'Check'){One 'old' 0 $true 12;One 'dense-pruned' 2 $true 12;One 'sparse' 1 $false 12}else{One 'time-a0' 0 $true 160;One 'time-b0' 1 $false 160;One 'time-b1' 1 $false 160;One 'time-a1' 0 $true 160}
