$ErrorActionPreference='Stop'
$root='D:\DLSSNR-Lab\mode1-runtime-ready-20261008'
$assets='C:\XboxGames\Onimusha- Way of the Sword\Content\DLSS5-AMD\native-game-tiled-assets'
$games='Shipping|SB-Win64|Onimusha|^re9$|Magpie|SandFall|Wuthering|Client-Win64|Genshin|YuanShen'
function Idle {& D:\DLSSNR-Lab\game-check.ps1 'SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game busy/check failed'};if(Get-Process|Where-Object {$_.ProcessName -match $games -or $_.ProcessName -eq 'rtc_compile'}){throw 'game/compiler active'}}
function Run($name,$dll,$mode,$model,$size,$height){
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName="$root\smoke.exe";$psi.Arguments="`"$dll`" `"$model\HIP`" $size 6 1";$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
 foreach($key in @($psi.EnvironmentVariables.Keys)){if($key.StartsWith('DLSS5_') -or $key.StartsWith('NR_')){$psi.EnvironmentVariables.Remove($key)}}
 $fixed=@{DLSS5_TEMPORAL_MODE="$mode";DLSS5_FAST_HISTORY='0';DLSS5_TEMPORAL_HISTORY_EXPERIMENT='0';DLSS5_MULTI_PASS='1';DLSS5_MULTI_PASS_PREDICT='0';DLSS5_MULTI_PASS_SKIN_PROTECT='0';DLSS5_VIT_ADAPTIVE='0';DLSS5_HIP_GRAPH='0';DLSS5_HIP_SUBMIT_PULSE='0';DLSS5_NETWORK_HEIGHT="$height";DLSS5_NETWORK_FREE_RES='0';DLSS5_SKIP_BLOCKS='';DLSS5_STYLE='1';DLSS5_FAST_NUMERIC='1';DLSS5_DIRECT_IO='1';LMXXF_WEIGHTS_DIR=$model;LMXXF_SHADER_DIR=$model;RT_RAW_PREFIX="$root\$name";TEMP="$root\cache";TMP="$root\cache";HIP_CACHE_DIR="$root\cache"}
 foreach($k in $fixed.Keys){$psi.EnvironmentVariables[$k]=$fixed[$k]}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$psi;if(!$p.Start()){throw 'start failed'};$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();$start=[DateTime]::UtcNow;$next=$start.AddSeconds(15)
 try {while(!$p.HasExited){Start-Sleep -Milliseconds 100;if([DateTime]::UtcNow -ge $next){if(Get-Process|Where-Object {$_.ProcessName -match $games}){throw 'game began'};$next=[DateTime]::UtcNow.AddSeconds(15)};if(([DateTime]::UtcNow-$start).TotalSeconds -gt 90){throw 'own smoke timeout no repeat'}}; $p.WaitForExit();if($p.ExitCode){throw "$name failed $($p.ExitCode)"}}
 finally {if(!$p.HasExited){$p.Kill();$p.WaitForExit()};$o.Result|Set-Content "$root\$name.stdout.log";$e.Result|Set-Content "$root\$name.stderr.log"}
}
foreach($p in @((Join-Path $root 'old.dll'),(Join-Path $root 'new.dll'),(Join-Path $root 'smoke.exe'),(Join-Path $root 'DLSS5-AMD\native-game-tiled-assets\post70-history-head.f16'))){if(!(Test-Path $p)){throw "preflight missing $p"}}
if((Get-FileHash "$root\new.dll").Hash -ne '3eaed204f5d6ad803749d6e8c79ed1e79c514f5d02ef1e68d8dcb3f292638db6'){throw 'new DLL identity'}

if((Get-FileHash "$root\old.dll").Hash -ne '0f3c3aec0ddcc74a684e11ec9bd62bf1a28001e1ad047af398e8f709aeecaf5f'){throw 'old DLL identity'}
if((Get-FileHash "$root\DLSS5-AMD\native-game-tiled-assets\post70-history-head.f16").Hash -ne '77c745c4e1d84a6227a2224291d83c37d1e9bd18d45e95add166b1174225ce8f'){throw 'row identity'}
if((Get-FileHash "$root\DLSS5-AMD\native-game-tiled-assets\HIP\gfx1201\c32-wave1.hsaco").Hash -ne 'c79e9c2a83ffd93b74089e0587be6b79f1b48b98e3ac8bbcdba45c40e71b0b24'){throw 'module identity c32-wave1.hsaco'}
if((Get-FileHash "$root\DLSS5-AMD\native-game-tiled-assets\HIP\gfx1201\c32-wave1-fast.hsaco").Hash -ne 'ff5b04c666fed772c72b8a0d30bb49f1bb3eacb887f211403e181caed0c9a2a7'){throw 'module identity c32-wave1-fast.hsaco'}
if((Get-FileHash "$root\DLSS5-AMD\native-game-tiled-assets\HIP\gfx1201\c32-wave1-fast-norm900.hsaco").Hash -ne '354eaa74d117b2a1becd0795b5fae5a5c8f869907d2f33affb7f70c0189f66e4'){throw 'module identity c32-wave1-fast-norm900.hsaco'}
if((Get-FileHash "$root\DLSS5-AMD\native-game-tiled-assets\HIP\gfx1201\c32-wave1-rtz.hsaco").Hash -ne '04c98fe3177f294ba981eca2c1c74782cf965b2b47843ae72e05e9f7e904d33a'){throw 'module identity c32-wave1-rtz.hsaco'}
Idle;if([IO.DriveInfo]::new('D:\').AvailableFreeSpace -lt 100GB){throw 'D free<100GB'}
New-Item -ItemType Directory -Force "$root\cache"|Out-Null
if(Test-Path "$root\720-old0-0.rgba16f"){throw 'existing output no repeat'}
$owner=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{Idle
 $newAssets="$root\DLSS5-AMD\native-game-tiled-assets"
 foreach($cfg in @(@{n='720';size='1280x720';height='720'},@{n='900';size='1600x900';height='900'})){
  $tag=$cfg.n
  Run "$tag-old0" "$root\old.dll" 0 $assets $cfg.size $cfg.height
  Run "$tag-new0" "$root\new.dll" 0 $newAssets $cfg.size $cfg.height
  Run "$tag-new1" "$root\new.dll" 1 $newAssets $cfg.size $cfg.height
  for($f=0;$f -lt 6;$f++){if((Get-FileHash "$root\$tag-old0-$f.rgba16f").Hash -ne (Get-FileHash "$root\$tag-new0-$f.rgba16f").Hash){throw "mode0 old/new mismatch $tag $f"}}
 }
 $log="$root\DLSS5-AMD\logs\temporal-mode.txt";if(!(Test-Path $log)){throw 'no actual mode1 log'};$lines=Get-Content $log;if(!($lines|Where-Object{$_ -match 'history_enabled=1'})){throw 'history admission never enabled'}
 'RUNTIME_MODE1_SMOKE_PASS MODE0_RAW_BIT0 REAL_SUBMISSION_FINITE NO_PERFORMANCE_NO_GAME'
}finally{$owner.Dispose();Remove-Item 'D:\DLSSNR-Lab\gpu.lock';'LOCK_RELEASED'}
