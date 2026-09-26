param([ValidateSet('hash','time','vram','vram3','smoke','all')][string]$What='all',[int]$TimeFrames=300)
$ErrorActionPreference='Continue'
$lab='D:\DLSSNR-Lab\re9-runtime-flags-20260926'
$bench="$lab\in\rt_bench.exe"
& 'D:\DLSSNR-Lab\hip-backend\check-idle.ps1'
if($LASTEXITCODE){throw 'GPU not idle'}
$off=@{DLSS5_HIP_WAVE_OWNED='0';DLSS5_HIP_C512_M32='0';DLSS5_HIP_VIT_PROJ_N64='0';DLSS5_HIP_PDL='0'}
function Run($tag,$which,$sizes,$frames,$rounds,$envs=@{}){
  foreach($k in @('DLSS5_HIP_WAVE_OWNED','DLSS5_HIP_C512_M32','DLSS5_HIP_VIT_PROJ_N64','DLSS5_HIP_PDL','DLSS5_NETWORK_HEIGHT')){Remove-Item "Env:$k" -ErrorAction SilentlyContinue}
  foreach($k in $envs.Keys){Set-Item "Env:$k" $envs[$k]}
  $out=& $bench "$lab\$which\LmxxfNrRuntime.dll" "$lab\$which\DLSS5-AMD\native-game-tiled-assets\HIP" $sizes $frames $rounds 2>&1
  foreach($l in $out){"$tag $l"}
}
if($What -in 'hash','all'){
  '== hash: same tier for all three runtimes'
  foreach($c in @(@{t='old';w='old';e=@{}},@{t='new';w='new';e=@{}},@{t='new-off';w='new';e=$off})){
    Run "$($c.t)" $c.w '1920x1080,1280x720' 24 1 $c.e
    foreach($nh in '1080','900'){ $e=$c.e.Clone(); $e['DLSS5_NETWORK_HEIGHT']=$nh; Run "$($c.t)@$nh" $c.w '1707x961' 24 1 $e }
  }
}
if($What -in 'time','all'){
  '== time: ABBA old/new, product settings (auto tier)'
  foreach($w in 'old','new','new','old'){ Run "time-$w" $w '1920x1080,1707x961' $TimeFrames 1 }
}
if($What -in 'vram','all'){
  '== vram: new runtime, 24 size switches'
  Run 'vram' 'new' '1920x1080,1280x720,1707x961' 8 8
}
if($What -eq 'vram3'){
  '== vram: old / new-off / new, 24 size switches each'
  Run 'vram-old' 'old' '1920x1080,1280x720,1707x961' 8 8
  Run 'vram-newoff' 'new' '1920x1080,1280x720,1707x961' 8 8 $off
  Run 'vram-new' 'new' '1920x1080,1280x720,1707x961' 8 8
}
if($What -in 'smoke','all'){
  '== runtime-smoke (single frame, RGB9E5 1506x848)'
  & 'D:\DLSSNR-Lab\re9-presr\runtime-smoke.exe' "$lab\new\LmxxfNrRuntime.dll" "$lab\new\DLSS5-AMD\native-game-tiled-assets\HIP" 2>&1 | ForEach-Object { "smoke $_" }
}
