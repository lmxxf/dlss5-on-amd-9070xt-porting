param([Parameter(Mandatory=$true)][string]$Manifest)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$r='D:\DLSSNR-Lab\gap-phase2-20261023';$lock='D:\DLSSNR-Lab\gpu.lock'
function Idle {
 & 'D:\DLSSNR-Lab\game-check.ps1' 'Shipping SB-Win64 Onimusha re9.exe Magpie SandFall Wuthering Client-Win64 Genshin YuanShen'|Out-Host
 if($LASTEXITCODE -ne 1){throw 'game-check not idle'}
 if((Get-PSDrive D).Free -lt 100GB){throw 'D free <100GB'}
}
function HashCheck($path,$sha){if((Get-FileHash $path -Algorithm SHA256).Hash.ToLower() -ne $sha.ToLower()){throw "SHA drift $path"}}
Idle
if(Get-Process|Where-Object {$_.ProcessName -match '^(benchmark|candidate|trace|candidate-trace|nr_graph|rtc_compile|frame|compat|probe|pure|frame-baseline|frame-candidate|pure-baseline|pure-candidate|trace-baseline|trace-candidate|final-frame|final-pure|final-trace)$'}){throw 'GPU/RTC job already present'}
$m=Get-Content $Manifest -Raw|ConvertFrom-Json
foreach($f in $m.files){HashCheck $f.path $f.sha256}
$owner=[IO.File]::Open($lock,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);$p=$null
try {
 $tag=[Text.Encoding]::UTF8.GetBytes('gap-phase2 '+$m.name);$owner.Write($tag,0,$tag.Length);$owner.Flush();Idle
 foreach($j in $m.jobs){
  Idle;$out=Join-Path $r $j.label;if(Test-Path $out){throw "exists $out; preserve evidence"};New-Item -ItemType Directory -Force $out|Out-Null
  $psi=[Diagnostics.ProcessStartInfo]::new();$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true;$psi.FileName=$j.exe;$psi.WorkingDirectory=$out
  foreach($key in @($psi.EnvironmentVariables.Keys)){if($key.StartsWith('DLSS5_') -or $key.StartsWith('NR_') -or $key.StartsWith('SP_') -or $key.StartsWith('SMALL_')){$psi.EnvironmentVariables.Remove($key)}}
  foreach($key in @('TEMP','TMP','HIP_CACHE_DIR')){$psi.EnvironmentVariables[$key]="$r\cache"};New-Item -ItemType Directory -Force "$r\cache"|Out-Null
  if($j.env){foreach($prop in $j.env.PSObject.Properties){$psi.EnvironmentVariables[$prop.Name]=[string]$prop.Value}}
  if($j.flags){$j.flags|Set-Content "$out\same.flags";New-Item -ItemType Directory -Force "$out\DLSS5-AMD\logs"|Out-Null;$j.flags|Set-Content "$out\DLSS5-AMD\native-game-flags.txt"}
  $psi.Arguments=$j.arguments.Replace('{OUT}',$out)
  @{label=$j.label;exe=$j.exe;arguments=$psi.Arguments;env=$j.env;start=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 8|Set-Content "$out\command.json"
  $p=[Diagnostics.Process]::new();$p.StartInfo=$psi;if(!$p.Start()){throw 'start failed'};$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();$t=[DateTime]::UtcNow;$next=$t.AddSeconds(15)
  try {while(!$p.HasExited){Start-Sleep -Milliseconds 100;if([DateTime]::UtcNow -ge $next){Idle;$next=[DateTime]::UtcNow.AddSeconds(15)};if(([DateTime]::UtcNow-$t).TotalSeconds -gt 180){throw 'own180s timeout'}}}finally{if(!$p.HasExited){$p.Kill()};$p.WaitForExit();$o.Result|Set-Content "$out\stdout.log";$e.Result|Set-Content "$out\stderr.log"}
  @{exit_code=$p.ExitCode;end=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content "$out\exit.json"
  if($p.ExitCode -ne 0){throw "failed $($j.label) exit=$($p.ExitCode)"}
  if($j.frame_count){$rows=@(Import-Csv "$out\out.csv");$checked=@($rows|Where-Object {$_.checked -eq '1'});$expected=if($j.edges_only){2}else{[int]$j.frame_count};if($rows.Count -ne [int]$j.frame_count -or $checked.Count -ne $expected -or @($checked|Where-Object {[int]$_.invalid -ne 0 -or [int]$_.changed_half_values_from_first -ne 0}).Count){throw "frame count/finite/repeat gate failed $($j.label)"}}
  if($null -ne $j.npair){$match=[regex]::Match($e.Result,'C512_NPAIR requested=(\d+) calls=(\d+)');if(!$match.Success -or [int]$match.Groups[1].Value -ne [int]$j.npair -or [long]$match.Groups[2].Value -ne [long]$j.npair_calls){throw "NPair actual route mismatch $($j.label)"}}
  $raw=@();foreach($f in Get-ChildItem $out -File|Where-Object {$_.Extension -in '.f16','.rgb32f','.rgba32f'}){$sha=(Get-FileHash $f.FullName).Hash.ToLower();$raw+=@{name=$f.Name;sha256=$sha;bytes=$f.Length};if($j.gold -and $sha -ne $j.gold){throw "Gold mismatch $($j.label) $($f.Name) $sha"};if($j.compare_to){$ref=(Get-FileHash $j.compare_to).Hash.ToLower();if($sha -ne $ref){throw "Paired Gold differs $($j.label) $($f.Name) $sha reference=$ref"}}}
  $raw|ConvertTo-Json -Depth 4|Set-Content "$out\raw-hashes.json"
  Write-Output ('DONE '+$j.label)
  Get-Content "$out\stdout.log"|Select-String 'RESULT |SYNC_NETWORK|GAP_COUNT|GAP_PROFILE|COMPAT|PASS|FAIL'
 }
 'ALL_JOBS_DONE '+$m.name
}finally{if($p -and !$p.HasExited){$p.Kill()};$owner.Dispose();Remove-Item $lock -Force;'LOCK_RELEASED'}
