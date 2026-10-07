$ErrorActionPreference='Stop'
$root='D:\DLSSNR-Lab\lowfreq-runtime-smoke-20261007'
$assets='C:\XboxGames\Onimusha- Way of the Sword\Content\DLSS5-AMD\native-game-tiled-assets'
$games='Shipping|SB-Win64|Onimusha|^re9$|Magpie|SandFall|Wuthering|Client-Win64|Genshin|YuanShen'
function Idle {& D:\DLSSNR-Lab\game-check.ps1 'SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game busy/check failed'};if(Get-Process|Where-Object {$_.ProcessName -match $games -or $_.ProcessName -eq 'rtc_compile'}){throw 'game/compiler active'}}
function Run($name,$dll,$mode){
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName="$root\smoke.exe";$psi.Arguments="`"$dll`" `"$assets\HIP`" 1280x720 4 1";$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
 foreach($key in @($psi.EnvironmentVariables.Keys)){if($key.StartsWith('DLSS5_') -or $key.StartsWith('NR_')){$psi.EnvironmentVariables.Remove($key)}}
 $fixed=@{DLSS5_TEMPORAL_MODE="$mode";DLSS5_FAST_HISTORY='0';DLSS5_TEMPORAL_HISTORY_EXPERIMENT='0';DLSS5_MULTI_PASS='1';DLSS5_MULTI_PASS_PREDICT='0';DLSS5_MULTI_PASS_SKIN_PROTECT='0';DLSS5_VIT_ADAPTIVE='0';DLSS5_HIP_GRAPH='0';DLSS5_HIP_SUBMIT_PULSE='0';DLSS5_NETWORK_HEIGHT='720';DLSS5_NETWORK_FREE_RES='0';DLSS5_SKIP_BLOCKS='';DLSS5_STYLE='1';DLSS5_FAST_NUMERIC='1';DLSS5_DIRECT_IO='1';LMXXF_WEIGHTS_DIR=$assets;LMXXF_SHADER_DIR=$assets;RT_RAW_PREFIX="$root\$name";TEMP="$root\cache";TMP="$root\cache";HIP_CACHE_DIR="$root\cache"}
 foreach($k in $fixed.Keys){$psi.EnvironmentVariables[$k]=$fixed[$k]}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$psi;if(!$p.Start()){throw 'start failed'};$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();$start=[DateTime]::UtcNow;$next=$start.AddSeconds(15)
 try {while(!$p.HasExited){Start-Sleep -Milliseconds 100;if([DateTime]::UtcNow -ge $next){if(Get-Process|Where-Object {$_.ProcessName -match $games}){throw 'game began'};$next=[DateTime]::UtcNow.AddSeconds(15)};if(([DateTime]::UtcNow-$start).TotalSeconds -gt 90){throw 'own smoke timeout no repeat'}}; $p.WaitForExit();if($p.ExitCode){throw "$name failed $($p.ExitCode)"}}
 finally {if(!$p.HasExited){$p.Kill();$p.WaitForExit()};$o.Result|Set-Content "$root\$name.stdout.log";$e.Result|Set-Content "$root\$name.stderr.log"}
}
Idle;if([IO.DriveInfo]::new('D:\').AvailableFreeSpace -lt 100GB){throw 'D free<100GB'}
New-Item -ItemType Directory -Force "$root\cache"|Out-Null
if(Test-Path "$root\old0-0.rgba16f"){throw 'existing output no repeat'}
$owner=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{Idle;Run 'old0' "$root\old.dll" 0;Run 'new0' "$root\new.dll" 0;Run 'new2' "$root\new.dll" 2
 for($f=0;$f -lt 4;$f++){if((Get-FileHash "$root\old0-$f.rgba16f").Hash -ne (Get-FileHash "$root\new0-$f.rgba16f").Hash){throw "mode0 old/new mismatch $f"}}
 foreach($f in 0,3){if((Get-FileHash "$root\new0-$f.rgba16f").Hash -ne (Get-FileHash "$root\new2-$f.rgba16f").Hash){throw "mode2 cold/reset mismatch $f"}}
 'RUNTIME_LOWFREQ_SMOKE_PASS NO_PERFORMANCE_NO_GAME'
}finally{$owner.Dispose();Remove-Item 'D:\DLSSNR-Lab\gpu.lock';'LOCK_RELEASED'}
