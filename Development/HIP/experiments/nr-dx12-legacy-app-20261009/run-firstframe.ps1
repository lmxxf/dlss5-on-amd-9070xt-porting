$ErrorActionPreference='Stop';$root='D:\DLSSNR-Lab\nr-dx12-legacy-app-20261009'
$games='Shipping|SB-Win64|Onimusha|^re9$|Magpie|SandFall|Wuthering|Client-Win64|Genshin|YuanShen'
function Idle {& D:\DLSSNR-Lab\game-check.ps1 'SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game busy/check failed'};if(Get-Process|Where-Object {$_.ProcessName -match $games -or $_.ProcessName -eq 'rtc_compile'}){throw 'game/compiler active'}}

Idle;if([IO.DriveInfo]::new('D:\').AvailableFreeSpace -lt 100GB){throw 'D low'}
$input='D:\DLSSNR-Lab\hip-backend\live-menu-before.f16';if((Get-Item $input).Length -ne 7464960 -or (Get-FileHash $input).Hash -ne 'FA3C18735AC5F105903614A18A9B1475966B9AFD9DD4AEB0CD9CAD7BC759EC9B'){throw 'input identity'}
if(Test-Path "$root\frame-first.f16"){throw 'existing output no repeat'};New-Item -ItemType Directory -Force "$root\cache"|Out-Null
if((Get-FileHash "$root\benchmark.exe").Hash -ne '358dcbbb8a3d7ad90d472fff58e7fd94a35cf3d7957d16b13ead6924d36472b8'){throw 'caller identity'};if(!(Test-Path "$root\assets\noise.f32") -and !(Test-Path "$root\assets\noise.f16")){throw 'noise missing'}
$owner=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {Idle;$psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName="$root\benchmark.exe";$psi.Arguments="`"D:\DLSSNR-Lab\nr-dx12-legacy-app-20261009\assets`" `"$root\legacy-full71.flags`" `"$input`" `"$root\frame`" 1 0";$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true;$psi.EnvironmentVariables['TEMP']="$root\cache";$psi.EnvironmentVariables['TMP']="$root\cache";$psi.EnvironmentVariables['HIP_CACHE_DIR']="$root\cache"
$p=[Diagnostics.Process]::new();$p.StartInfo=$psi;if(!$p.Start()){throw 'start'};$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();$t=[DateTime]::UtcNow;$next=$t.AddSeconds(15)
try {while(!$p.HasExited){Start-Sleep -Milliseconds 100;if([DateTime]::UtcNow -ge $next){if(Get-Process|Where-Object{$_.ProcessName -match $games}){throw 'game started'};$next=[DateTime]::UtcNow.AddSeconds(15)};if(([DateTime]::UtcNow-$t).TotalSeconds -gt 90){throw 'own timeout no repeat'}};$p.WaitForExit();if($p.ExitCode){throw "child failed $($p.ExitCode)"}}
finally {if(!$p.HasExited){$p.Kill();$p.WaitForExit()};$o.Result|Set-Content "$root\stdout.log";$e.Result|Set-Content "$root\stderr.log"}
'LEGACY_FULL_APP_FIRSTFRAME_DONE_DIAGNOSTIC_NOT_PERF'}finally{$owner.Dispose();Remove-Item 'D:\DLSSNR-Lab\gpu.lock';'LOCK_RELEASED'}
