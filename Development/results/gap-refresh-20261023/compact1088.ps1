param([ValidateSet('gold','timing')][string]$Action='gold')
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$r='D:\DLSSNR-Lab\gap-refresh-20261023';$fixture='D:\DLSSNR-Lab\sync-network-gap1080-20261006';$assets='D:\DLSSNR-Lab\history-trial-041a-20261006\assets';$lock='D:\DLSSNR-Lab\gpu.lock'
function CheckGame {& 'D:\DLSSNR-Lab\game-check.ps1' 'Shipping SB-Win64 Onimusha re9.exe Magpie SandFall Wuthering Client-Win64'|Out-Host;if($LASTEXITCODE -ne 1){throw 'game-check not idle'}}
CheckGame
if(Get-Process|Where-Object {$_.ProcessName -match '^(benchmark|candidate|trace|candidate-trace|nr_graph|rtc_compile|frame)$'}){throw 'GPU/RTC job exists'}
if((Get-PSDrive D).Free -lt 100GB){throw 'free<100GB'}
foreach($x in (Get-Content "$r\candidate-exes.json" -Raw|ConvertFrom-Json)){if((Get-FileHash (Join-Path $r $x.name)).Hash.ToLower() -ne $x.sha256){throw "bad exe $($x.name)"}}
foreach($x in (Get-Content "$r\current-module-hashes.json" -Raw|ConvertFrom-Json)){if((Get-FileHash (Join-Path "$r\modules" $x.name)).Hash.ToLower() -ne $x.sha256){throw "bad module $($x.name)"}}
if((Get-FileHash "$fixture\fixture\processing1088.rgba32f").Hash.ToLower() -ne '3ef42d34cebd1bc847e5caa3a95b3c3b70f45c9eeed4e828a3e2f85615b49b68'){throw 'input drift'}
$root="$r\compact1088";New-Item -ItemType Directory -Force $root|Out-Null
if($Action -eq 'timing' -and !(Test-Path "$root\gold-pass.json")){throw 'gold prerequisite absent'}
$owner=[IO.File]::Open($lock,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);$p=$null
try {
 $tag=[Text.Encoding]::UTF8.GetBytes('gap-refresh compact1088 '+$Action);$owner.Write($tag,0,$tag.Length);$owner.Flush();CheckGame
 $jobs=@()
 if($Action -eq 'gold'){$jobs=@(@{label='trace-current';exe='trace.exe';warm=0;frames=3},@{label='trace-candidate';exe='candidate-trace.exe';warm=0;frames=3},@{label='gold-candidate';exe='candidate.exe';warm=1;frames=1})}
 else {foreach($round in 1..3){$slot=0;foreach($kind in @('N','C','C','N')){$jobs+=@{label="r$round-$slot-$kind";exe=$(if($kind -eq 'N'){'benchmark.exe'}else{'candidate.exe'});warm=80;frames=160};$slot++}}}
 $raw=@()
 foreach($j in $jobs){
  CheckGame;if((Get-PSDrive D).Free -lt 100GB){throw 'free<100GB'}
  $out=Join-Path $root $j.label;if(Test-Path $out){throw "exists $out"};New-Item -ItemType Directory $out|Out-Null
  $psi=New-Object Diagnostics.ProcessStartInfo;$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true;$psi.WorkingDirectory=$r;$psi.FileName=Join-Path $r $j.exe
  foreach($key in @($psi.EnvironmentVariables.Keys)){if($key.StartsWith('DLSS5_') -or $key.StartsWith('NR_')){$psi.EnvironmentVariables.Remove($key)}}
  foreach($key in @('TEMP','TMP','HIP_CACHE_DIR')){$psi.EnvironmentVariables[$key]="$r\cache"}
  $psi.Arguments="$assets $r\modules $fixture\fast1-1088.flags $fixture\fixture\processing1088.rgba32f $out $($j.warm) $($j.frames) 1088"
  @{label=$j.label;exe=$j.exe;arguments=$psi.Arguments;started=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content "$out\command.json"
  $p=New-Object Diagnostics.Process;$p.StartInfo=$psi;if(!$p.Start()){throw 'start failed'};$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$t=[DateTime]::UtcNow;$next=$t.AddSeconds(15)
  try {while(!$p.HasExited){Start-Sleep -Milliseconds 100;if([DateTime]::UtcNow -ge $next){CheckGame;$next=[DateTime]::UtcNow.AddSeconds(15)};if(([DateTime]::UtcNow-$t).TotalSeconds -gt 120){throw 'own probe timeout'}}}finally{if(!$p.HasExited){$p.Kill()};$p.WaitForExit();$stdout.Result|Set-Content "$out\stdout.log";$stderr.Result|Set-Content "$out\stderr.log"}
  if($p.ExitCode -ne 0){throw "failed $($j.label) exit=$($p.ExitCode)"}
  foreach($f in Get-ChildItem $out -Filter '*.rgb32f'){$sha=(Get-FileHash $f.FullName).Hash.ToLower();$raw+=@{label=$j.label;file=$f.Name;sha256=$sha;bytes=$f.Length};if($sha -ne '153ac018f5dd5744cff9157661c46c469d01db6018a97c93aec2b1e2e05647f1'){throw "Gold differs $($j.label) $sha"}}
  $raw|ConvertTo-Json -Depth 4|Set-Content "$root\$Action-raw.json"
  Write-Output $j.label;Get-Content "$out\stdout.log"|Select-String 'GAP_PROFILE|GAP_COUNT|GAP_TRACE_DONE|SYNC_NETWORK'
 }
 if($Action -eq 'gold'){@{pass=$true;all_sha='153ac018f5dd5744cff9157661c46c469d01db6018a97c93aec2b1e2e05647f1';jobs=3}|ConvertTo-Json|Set-Content "$root\gold-pass.json"}
 'COMPACT1088_DONE_'+$Action
}finally{if($p -and !$p.HasExited){$p.Kill()};$owner.Dispose();Remove-Item $lock -Force;'LOCK_RELEASED'}
