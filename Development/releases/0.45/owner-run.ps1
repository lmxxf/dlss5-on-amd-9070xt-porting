$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$r='D:\DLSSNR-Lab\release-045';$held=$null;$p=$null
function Idle {& D:\DLSSNR-Lab\game-check.ps1 'Shipping SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game active'};if((Get-PSDrive D).Free -lt 100GB){throw 'disk below100GB'}}
try{
 Idle;if(@(Get-Process|Where-Object {$_.ProcessName -match '^(frame|pure|benchmark|rtc_compile.*|runtime-smoke|compile_fit_shaders)$'}).Count){throw 'other experiment active'}
 $held=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);Idle
 $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName='powershell.exe';$si.Arguments='-NoProfile -ExecutionPolicy Bypass -File D:\DLSSNR-Lab\release-045\run-package.ps1 -SourceCommit c24e3a97cdf596d0c997cee7deda91c981d520ce';$si.UseShellExecute=$false;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true;$si.WorkingDirectory=$r
 $p=[Diagnostics.Process]::new();$p.StartInfo=$si;if(!$p.Start()){throw 'package start failed'};$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();$start=[DateTime]::UtcNow;$next=$start.AddSeconds(15)
 try{while(!$p.HasExited){Start-Sleep -Milliseconds 200;if([DateTime]::UtcNow -ge $next){Idle;$next=[DateTime]::UtcNow.AddSeconds(15)};if(([DateTime]::UtcNow-$start).TotalSeconds -gt 3600){throw 'package own3600s timeout'}};$p.WaitForExit()}finally{if(!$p.HasExited){taskkill /PID $p.Id /T /F|Out-Null;$p.WaitForExit()};[IO.File]::WriteAllText("$r\package.stdout.log",$o.Result,[Text.UTF8Encoding]::new($false));[IO.File]::WriteAllText("$r\package.stderr.log",$e.Result,[Text.UTF8Encoding]::new($false))}
 @{exit_code=$p.ExitCode;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -Encoding UTF8 "$r\package-exit.json"
 if($p.ExitCode){throw "package exit=$($p.ExitCode)"};'PACKAGE_045_COMPLETE'
}finally{if($p -and !$p.HasExited){taskkill /PID $p.Id /T /F|Out-Null};if($held){$held.Dispose();Remove-Item D:\DLSSNR-Lab\gpu.lock};'LOCK_RELEASED'}
