$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\nr-dx12-matrix-probe-20261009'
$games='Shipping|SB-Win64|Onimusha|^re9$|Magpie|SandFall|Wuthering|Client-Win64|Genshin|YuanShen'
function Idle {& D:\DLSSNR-Lab\game-check.ps1 'SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game busy/check failed'};if(Get-Process|Where-Object {$_.ProcessName -match $games -or $_.ProcessName -eq 'rtc_compile'}){throw 'game/compiler active'}}
Idle;if([IO.DriveInfo]::new('D:\').AvailableFreeSpace -lt 100GB){throw 'D low'}
if(Test-Path "$r\fp8-raw.bin"){throw 'existing output no repeat'}
$owner=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {
Idle
foreach($type in @('fp8','f16')) {
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName="$r\probe.exe";$psi.WorkingDirectory=$r
 $psi.Arguments="`"$r\probe-$type.cso`" `"$r\input`" `"$r\$type`"";if($type -eq 'f16'){$psi.Arguments+=' f16'}
 $psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
 $p=[Diagnostics.Process]::new();$p.StartInfo=$psi;if(!$p.Start()){throw 'start'};$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();$start=[DateTime]::UtcNow;$next=$start.AddSeconds(15)
 try {while(!$p.HasExited){Start-Sleep -Milliseconds 100;if([DateTime]::UtcNow -ge $next){Idle;$next=[DateTime]::UtcNow.AddSeconds(15)};if(([DateTime]::UtcNow-$start).TotalSeconds -gt 45){throw 'own timeout'}};$p.WaitForExit();if($p.ExitCode){throw "probe $type failed $($p.ExitCode)"}}
 finally {if(!$p.HasExited){$p.Kill();$p.WaitForExit()};$o.Result|Set-Content "$r\$type-stdout.log";$e.Result|Set-Content "$r\$type-stderr.log"}
}
'PRIMITIVE_RUN_DONE_NOT_FULL_NETWORK'
}finally{$owner.Dispose();Remove-Item 'D:\DLSSNR-Lab\gpu.lock';'LOCK_RELEASED'}
