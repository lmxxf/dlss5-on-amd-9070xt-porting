param([string]$Variant='fragment',[string]$Thresholds='0.5,1')
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend';$lab='D:\DLSSNR-Lab';$b="$lab\Magpie-DLSS5-AMD-0.23\DLSS5-AMD";$work=if($Variant -eq 'fragment'){"$r\vit-motion-results"}else{"$r\vit-motion-$Variant-results"};New-Item -ItemType Directory -Force $work|Out-Null
$flags=@(Get-Content "$b\native-game-flags.txt")+@('DLSS5_HIP_MH_FEATURE_BYTE=1','DLSS5_HIP_MH_PROJ_DIAG_FB=1','DLSS5_HIP_MH_BYTE_STREAM=1','DLSS5_HIP_DECODER_BYTE=1','DLSS5_HIP_VIT_BYTE_STREAM=0','DLSS5_HIP_MH_FFN_FRAG256=1','DLSS5_HIP_GRAPH=0','DLSS5_SHOW_FPS=0','DLSS5_PRE_UPSCALE=0','DLSS5_NETWORK_HEIGHT=900','DLSS5_TEST_TRANSLATE=1')
foreach($v in (@('base','-1')+$Thresholds.Split(','))){
 if(Get-Process re9,SB-Win64-Shipping,LOP-Win64-Shipping,Magpie -ErrorAction SilentlyContinue){throw 'Game running'}
 $m=if($v -eq 'base'){"$r\vit-stream-exact-modules"}else{"$r\vit-$Variant-modules"};$t=if($v -eq 'base'){''}else{$v};$diag=if($v -eq 'base'){''}else{"$work\$v-route.csv"}
 if($diag -and (Test-Path $diag)){Remove-Item $diag}
 $f="$work\flags.txt";[IO.File]::WriteAllLines($f,($flags+@("DLSS5_VIT_SELECT_THRESHOLD=$t","DLSS5_VIT_SELECT_DIAG=$diag")))
 & "$r\benchmark_vit_motion.exe" "$b\native-game-tiled-assets" $f "$r\live-menu-before.f16" "$work\$v" 12 0 $m 0 0 0 0 > "$work\$v.log"
 if($LASTEXITCODE){throw 'Motion replay failed'}
 $rows=@(Import-Csv "$work\$v.csv");if($rows.Count -ne 12 -or @($rows|Where-Object{[int]$_.invalid -ne 0}).Count){throw 'Invalid motion output'}
 $expected=if($v -in 'base','-1'){"$r\vit-select-$Variant-results\base-900-p0.f16"}else{"$r\vit-select-$Variant-results\select-$v-900-p0.f16"}
 if((Get-FileHash "$work\$v-motion-0.f16").Hash -ne (Get-FileHash $expected).Hash){throw 'Motion frame zero differs from static replay'}
 if($v -eq '-1'){foreach($i in 0..11){if((Get-FileHash "$work\-1-motion-$i.f16").Hash -ne (Get-FileHash "$work\base-motion-$i.f16").Hash){throw 'Motion exact control differs'}}}
 "${v}: 12 frames finite; frame zero matched static reference"
}
