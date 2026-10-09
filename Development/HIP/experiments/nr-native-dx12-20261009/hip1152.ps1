$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\nr-dx12-pure-20261009';$dir="$r\hip1152";$exe='D:\DLSSNR-Lab\fresh-mochi1088-20261006\benchmark.exe';$assets='D:\DLSSNR-Lab\history-trial-041a-20261006\assets';$mods='D:\DLSSNR-Lab\combined-vs041-20261006\modules-current';$flags='D:\DLSSNR-Lab\sync-network-gap1080-20261006\fast1-1152.flags';$input="$r\processing1152.rgba32f"
function Idle{& D:\DLSSNR-Lab\game-check.ps1 'SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game/check live'};if(Get-Process -Name rtc_compile -ErrorAction SilentlyContinue){throw 'RTC live'}}
Idle;if([IO.DriveInfo]::new('D:\').AvailableFreeSpace -lt 100GB){throw 'disk<100GB'};if(Test-Path "$dir\first.rgb32f"){throw 'existing reference no retry'}
if((Get-FileHash $input).Hash -ne 'B3D114C4779FC1F593162B36D22BBB5AE7777E22E7AEC701F0F551916539B2AC'){throw 'inputSHA'}
New-Item -ItemType Directory -Force "$dir\cache"|Out-Null
Get-FileHash $exe,$flags|ConvertTo-Json|Set-Content "$dir\source.json"
$owner=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$exe;$psi.Arguments="`"$assets`" `"$mods`" `"$flags`" `"$input`" `"$dir`" 10 20 1152";$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
foreach($k in @($psi.EnvironmentVariables.Keys)){if($k.StartsWith('DLSS5_')-or $k.StartsWith('NR_')-or $k.StartsWith('SP_')){$psi.EnvironmentVariables.Remove($k)}};foreach($k in @('TEMP','TMP','HIP_CACHE_DIR')){$psi.EnvironmentVariables[$k]="$dir\cache"}
$p=[Diagnostics.Process]::new();$p.StartInfo=$psi;[void]$p.Start();"OWN_PID=$($p.Id)";$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();$start=[DateTime]::UtcNow;$next=$start.AddSeconds(15)
try{while(!$p.HasExited){Start-Sleep -Milliseconds 100;if([DateTime]::UtcNow -ge $next){Idle;$next=[DateTime]::UtcNow.AddSeconds(15)};if(([DateTime]::UtcNow-$start).TotalSeconds -gt 90){throw 'own pilot timeout'}};$p.WaitForExit();if($p.ExitCode){throw "HIP exit $($p.ExitCode)"}}
finally{if(!$p.HasExited){$p.Kill();$p.WaitForExit()};$o.Result|Set-Content "$dir\stdout.log";$e.Result|Set-Content "$dir\stderr.log"}
'FRESH_HIP_1152_PILOT_PASS'
}finally{$owner.Dispose();Remove-Item 'D:\DLSSNR-Lab\gpu.lock';'LOCK_RELEASED'}
