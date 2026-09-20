$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend';$b='D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.23\DLSS5-AMD';$out="$r\vit-adaptive-image-span-results"
if(Get-Process re9,SB-Win64-Shipping,LOP-Win64-Shipping,Magpie -ErrorAction SilentlyContinue){throw 'Game/Magpie running'}
New-Item -ItemType Directory -Force $out|Out-Null
foreach($kind in 0,1,2){
 $flags=@(Get-Content "$r\vit-adaptive-image-schedule-1-timing-results\timing-0-900-s1\flags.txt")+@('DLSS5_HIP_SPAN_PROBE=1',"DLSS5_VIT_IMAGE_SCHEDULE=$kind")
 [IO.File]::WriteAllLines("$out\flags-$kind.txt",$flags)
 # cmd handles native stderr separately; diagnostic stderr is expected, not a PowerShell error record.
 $line='"'+"$r\benchmark_vit_image_overlap.exe"+'" "'+"$b\native-game-tiled-assets"+'" "'+"$out\flags-$kind.txt"+'" "'+"$r\live-menu-before.f16"+'" "'+"$out\rgb-$kind"+'" 40 1 "'+"$r\vit-residual-adaptive-r3-modules"+'" 0 1 0 0 > "'+"$out\run-$kind.log"+'" 2> "'+"$out\span-$kind.log"+'"'
 & cmd /d /c $line;if($LASTEXITCODE){throw "Span probe failed kind=$kind"}
 Write-Output "SPAN_DONE kind=$kind"
}
