# Replay against the actual installed assets, with NO explicit HIP_MODULES argument.
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend';$p='D:\DLSSNR-Lab\AttExp-preview'
$g='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64';$root="$g\DLSS5-AMD";$assets="$root\native-game-tiled-assets";$d="$r\stellar-adaptive-installed-check"
if(Get-Process SB-Win64-Shipping,re9,LOP-Win64-Shipping,Magpie -ErrorAction SilentlyContinue){throw 'Close game/Magpie before replay'}
$manifest=Get-Content "$p\manifest.json" -Raw|ConvertFrom-Json
foreach($f in $manifest){if((Get-FileHash (Join-Path $g $f.name)).Hash -ne $f.sha256){throw "Installed mismatch: $($f.name)"}}
if(Test-Path "$root\HIP"){throw 'Unexpected stale module directory outside runtime assets'}
New-Item -ItemType Directory -Force $d|Out-Null
foreach($mode in 0,1){
 $flags=@(Get-Content "$root\native-game-flags.txt")+@('DLSS5_HIP_MODULES=','DLSS5_NETWORK_HEIGHT=900',"DLSS5_VIT_ADAPTIVE=$mode",'DLSS5_VIT_REUSE_HOTKEY=0','DLSS5_VIT_ADAPTIVE_LOG=','DLSS5_RESIDUAL_SEQUENCE=0','DLSS5_RESIDUAL_RGB=0')
 [IO.File]::WriteAllLines("$d\flags-$mode.txt",$flags,(New-Object Text.UTF8Encoding($false)))
 & "$r\benchmark_vit_adaptive.exe" $assets "$d\flags-$mode.txt" "$r\live-menu-before.f16" "$d\rgb-$mode" 12 0 > "$d\run-$mode.log"
 if($LASTEXITCODE){throw "Installed-default replay failed mode=$mode"}
 $rows=@(Import-Csv "$d\rgb-$mode.csv");if($rows.Count -ne 12 -or @($rows|Where-Object{[int]$_.invalid -ne 0 -or [int]$_.changed_half_values_from_first -ne 0}).Count){throw 'Missing/nonfinite/changed frozen output'}
 Write-Output "INSTALLED_DEFAULT_PASS mode=$mode hash=$((Get-FileHash "$d\rgb-$mode.f16").Hash)"
}
if((Get-FileHash "$d\rgb-0.f16").Hash -ne (Get-FileHash "$d\rgb-1.f16").Hash){throw 'Installed adaptive/exact output mismatch'}
"VERIFIED_FILES=$($manifest.Count) DEFAULT_MODULES=$assets\HIP"|Tee-Object "$d\result.txt"
