$ErrorActionPreference='Stop'
$root='D:\DLSSNR-Lab\temporal-mode3-20261008'
function Idle {& D:\DLSSNR-Lab\game-check.ps1 'SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game active/check failed'}}
Idle
if([IO.DriveInfo]::new('D:\').AvailableFreeSpace -lt 100GB){throw 'free disk<100GB'}
New-Item -ItemType Directory -Force "$root\cache"|Out-Null
$owner=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {
 foreach($case in @('math','mode2-regression')){foreach($mode in @('warp','amd')){
  Idle;$psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName="$root\$case.exe";$psi.Arguments=if($mode -eq 'amd'){'--gpu'}else{''};$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true;$psi.EnvironmentVariables['TEMP']="$root\cache";$psi.EnvironmentVariables['TMP']="$root\cache"
  $p=[Diagnostics.Process]::new();$p.StartInfo=$psi;[void]$p.Start();$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();$start=[DateTime]::UtcNow;$next=$start.AddSeconds(15)
  try{while(!$p.HasExited){Start-Sleep -Milliseconds 100;if([DateTime]::UtcNow -ge $next){Idle;$next=[DateTime]::UtcNow.AddSeconds(15)};if(([DateTime]::UtcNow-$start).TotalSeconds -gt 60){throw 'own bounded timeout'}};$p.WaitForExit();if($p.ExitCode){throw "$case/$mode failed exit $($p.ExitCode)"}}
  finally {if(!$p.HasExited){$p.Kill();$p.WaitForExit()};$o.Result|Set-Content "$root\$case-$mode.stdout.log";$e.Result|Set-Content "$root\$case-$mode.stderr.log"}
 }}
 'MODE3_AND_MODE2_REGRESSION_PASS'
}finally{$owner.Dispose();Remove-Item 'D:\DLSSNR-Lab\gpu.lock';'LOCK_RELEASED'}
