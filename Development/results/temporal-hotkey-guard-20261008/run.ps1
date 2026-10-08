$ErrorActionPreference='Stop'
$root='D:\DLSSNR-Lab\temporal-hotkey-guard-20261008'
New-Item -ItemType Directory -Force "$root\DLSS5-AMD\logs"|Out-Null
'DLSS5_TEMPORAL_MODE=3'|Set-Content "$root\DLSS5-AMD\default-config.txt"
[IO.File]::WriteAllText("$root\DLSS5-AMD\custom-config.txt","DLSS5_MULTI_PASS=1`r`nDLSS5_TEMPORAL_MODE=3`r`n")
[IO.File]::WriteAllText("$root\DLSS5-AMD\native-game-flags.txt","DLSS5_MULTI_PASS=1`r`n")
$psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName="$root\test.exe";$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
$p=[Diagnostics.Process]::new();$p.StartInfo=$psi;[void]$p.Start();$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();if(!$p.WaitForExit(10000)){$p.Kill();throw 'own CPU test timeout'};$o.Result|Set-Content "$root\test.stdout.log";$e.Result|Set-Content "$root\test.stderr.log";if($p.ExitCode){throw "isolated CPU test exit $($p.ExitCode)"}
'CPU_ONLY_PASS NO_GPU_NO_GAME_CONFIG'
