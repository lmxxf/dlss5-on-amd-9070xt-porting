param([ValidateSet("gold","pilot")][string]$Phase="gold")
$ErrorActionPreference='Stop';$prefix=if($Phase -eq 'pilot'){'pilot'}else{'gold'};$warm=if($Phase -eq 'pilot'){10}else{0};$measure=if($Phase -eq 'pilot'){20}else{0}
$r='D:\DLSSNR-Lab\nr-dx12-pure-20261009';$assets='D:\DLSSNR-Lab\nr-dx12-pure-20261009\assets';$input='D:\DLSSNR-Lab\nr-dx12-pure-20261009\processing1152.rgba32f'
function Idle {& D:\DLSSNR-Lab\game-check.ps1 'SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game/check busy'};if(Get-Process -Name rtc_compile -ErrorAction SilentlyContinue){throw 'RTC live'}}
Idle;if([IO.DriveInfo]::new('D:\').AvailableFreeSpace -lt 100GB){throw 'disk<100GB'}
if((Get-FileHash $input).Hash -ne 'B3D114C4779FC1F593162B36D22BBB5AE7777E22E7AEC701F0F551916539B2AC'){throw 'inputSHA'}
New-Item -ItemType Directory -Force "$r\cache","$r\psos"|Out-Null
if(Test-Path "$r\$prefix-first.rgb32f"){throw 'existing output no blind rerun'}
$owner=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName="$r\pure.exe";$psi.Arguments="`"$assets`" `"$input`" `"$r\$prefix`" $warm $measure";$psi.WorkingDirectory=$r;$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
foreach($k in @($psi.EnvironmentVariables.Keys)){if($k.StartsWith('DLSS5_') -or $k.StartsWith('NR_')){$psi.EnvironmentVariables.Remove($k)}}
$psi.EnvironmentVariables['NR_DX12_ROWS']='1152';$psi.EnvironmentVariables['NR_DX12_FLAGS']="$r\full71.flags";if($Phase -eq "gold"){$psi.EnvironmentVariables['NR_DX12_PSO_DIR']="$r\psos"};$psi.EnvironmentVariables['DLSS5_SHADER_DISK_CACHE']='0';$psi.EnvironmentVariables['TEMP']="$r\cache";$psi.EnvironmentVariables['TMP']="$r\cache"
$p=[Diagnostics.Process]::new();$p.StartInfo=$psi;[void]$p.Start();"OWN_PID=$($p.Id)";$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();$start=[DateTime]::UtcNow;$next=$start.AddSeconds(15)
try{while(!$p.HasExited){Start-Sleep -Milliseconds 100;if([DateTime]::UtcNow -ge $next){Idle;$next=[DateTime]::UtcNow.AddSeconds(15)};if(([DateTime]::UtcNow-$start).TotalSeconds -gt 180){throw 'own full71 timeout no retry'}};$p.WaitForExit();if($p.ExitCode){throw "pure failed $($p.ExitCode)"}}
finally{if(!$p.HasExited){$p.Kill();$p.WaitForExit()};$o.Result|Set-Content "$r\$prefix.stdout.log";$e.Result|Set-Content "$r\$prefix.stderr.log"}
'LEGACY_FULL71_GOLD_PROCESS_PASS'
}finally{$owner.Dispose();Remove-Item 'D:\DLSSNR-Lab\gpu.lock';'LOCK_RELEASED'}
