param([Parameter(Mandatory=$true)][string]$Original,[Parameter(Mandatory=$true)][string]$Before)
$ErrorActionPreference='Stop'
$root='D:\DLSSNR-Lab\mode3-real-output-20261008'
function Idle {& D:\DLSSNR-Lab\game-check.ps1 'SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game live/check failed; no GPU probe'}}
Idle
if([IO.DriveInfo]::new('D:\').AvailableFreeSpace -lt 100GB){throw 'disk<100GB'}
# These identities are the existing coupled encoded-gradient/fullNN snapshot, not current game/HDR output.
if((Get-FileHash $Original).Hash -ne '3EF42D34CEBD1BC847E5CAA3A95B3C3B70F45C9EEED4E828A3E2F85615B49B68'){throw 'original encoded fixture identity'}
if((Get-FileHash $Before).Hash -ne '153AC018F5DD5744CFF9157661C46C469D01DB6018A97C93AEC2B1E2E05647F1'){throw 'paired fullNN output identity'}
New-Item -ItemType Directory -Force "$root\cache"|Out-Null
$owner=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {$psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName="$root\replay.exe";$psi.Arguments="`"$Original`" `"$Before`" `"$root\after.rgb32f`" 1920 1080 1088 1 1";$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true;$psi.EnvironmentVariables['TEMP']="$root\cache";$psi.EnvironmentVariables['TMP']="$root\cache";$p=[Diagnostics.Process]::new();$p.StartInfo=$psi;[void]$p.Start();$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();$start=[DateTime]::UtcNow;$next=$start.AddSeconds(15)
 try {while(!$p.HasExited){Start-Sleep -Milliseconds 100;if([DateTime]::UtcNow -ge $next){Idle;$next=[DateTime]::UtcNow.AddSeconds(15)};if(([DateTime]::UtcNow-$start).TotalSeconds -gt 60){throw 'own probe timeout'}};$p.WaitForExit();if($p.ExitCode){throw "replay exit $($p.ExitCode)"}}
 finally{if(!$p.HasExited){$p.Kill();$p.WaitForExit()};$o.Result|Set-Content "$root\stdout.log";$e.Result|Set-Content "$root\stderr.log"}
 'CAPTURED_FULLNN_BUFFER_REPLAY_PASS NOT_CURRENT_GAME_NOT_DECODED'
}finally{$owner.Dispose();Remove-Item 'D:\DLSSNR-Lab\gpu.lock';'LOCK_RELEASED'}
