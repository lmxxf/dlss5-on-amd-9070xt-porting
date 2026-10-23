$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$r='D:\DLSSNR-Lab\gap-phase3-20261023\current-gap';$fixtureRoot='D:\DLSSNR-Lab\sync-network-gap1080-20261006';$oldModules='D:\DLSSNR-Lab\combined-vs041-20261006\modules-current';$modules="$r\modules";$assets='D:\DLSSNR-Lab\history-trial-041a-20261006\assets';$mz='D:\DLSSNR-Lab\competitor-timing-20260930\mz';$lock='D:\DLSSNR-Lab\gpu.lock'
function CheckGame {
 & 'D:\DLSSNR-Lab\game-check.ps1' 'Shipping SB-Win64 Onimusha re9.exe Magpie SandFall Wuthering Client-Win64' | Out-Host
 if($LASTEXITCODE -ne 1){throw 'Game check not idle; stop own probe only'}
}
function HashCheck($path,$expected){$actual=(Get-FileHash $path -Algorithm SHA256).Hash.ToLower();if($actual -ne $expected.ToLower()){throw "Hash mismatch $path expected=$expected actual=$actual"}}
CheckGame
if(Get-Process | Where-Object {$_.ProcessName -match '^(benchmark|nr_graph|rtc_compile|frame|frame-warmed|frame-probe|integrated-probe)$'}){throw 'Another GPU/RTC process'}
if((Get-PSDrive D).Free -lt 100GB){throw 'D free<100GB'}
HashCheck "$mz\nr_graph.exe" '8ad3ac1cfd5d83f58fb9a223e21d7a970118518839bada3abcaba66af5e8d153'
HashCheck "$mz\dlssnr.bin" '2b41c888cf4155b8958c665ba64018ab0bd25c85fc71a2b6db86d0d04d1f7fbd'
HashCheck 'D:\DLSSNR-Lab\fresh-mochi1088-20261006\benchmark.exe' '1efec59daee07c6e32759f8f1c304337fc047df433eff7d8abf3f4c8b453e4fd'
HashCheck "$fixtureRoot\fixture\processing1088.rgba32f" '3ef42d34cebd1bc847e5caa3a95b3c3b70f45c9eeed4e828a3e2f85615b49b68'
HashCheck "$fixtureRoot\fixture\valid.rgba32f" '17b2e09bf9757a5a98fbd65a08f618100ec42f4d7abb7036cb03cb19e3ae4e2a'

HashCheck "$r\benchmark.exe" '543a4b6a2badb13a2fb5c98ffa38061472355f478636a5f895d4ec05f6a82f63'
HashCheck "$fixtureRoot\fast1-1088.flags" 'e41916ae81d138a424ce3da8f174034f3af9c6783bdb35909eadbffe22eadcdb'
foreach($m in (Get-Content "$r\mochi-asset-hashes.json" -Raw | ConvertFrom-Json)){HashCheck (Join-Path $mz $m.path) $m.sha256}
foreach($m in (Get-Content "$r\old-module-hashes.json" -Raw | ConvertFrom-Json)){HashCheck (Join-Path $oldModules $m.name) $m.sha256}
foreach($m in (Get-Content "$r\current-module-hashes.json" -Raw | ConvertFrom-Json)){HashCheck (Join-Path $modules $m.name) $m.sha256}
New-Item -ItemType Directory -Force "$r\cache" | Out-Null
Copy-Item "$mz\pc.bin" "$r\mochi-cache.bin"
$before=@(Get-ChildItem $mz -File -Recurse | ForEach-Object { $_.FullName+'|'+$_.Length+'|'+$_.LastWriteTimeUtc.Ticks})
$owner=[IO.File]::Open($lock,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);$p=$null
try {
 $tag=[Text.Encoding]::UTF8.GetBytes('gap-phase3-current-gap root');$owner.Write($tag,0,$tag.Length);$owner.Flush();CheckGame
 Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion | ConvertTo-Json | Set-Content "$r\driver.json"
 Get-PSDrive D | Select-Object Name,Free,Used | ConvertTo-Json | Set-Content "$r\disk-start.json"
 $identity=@();foreach($path in @("$r\benchmark.exe",'D:\DLSSNR-Lab\fresh-mochi1088-20261006\benchmark.exe',"$mz\nr_graph.exe","$mz\plan-1920x1080.txt","$mz\dlssnr.bin","$fixtureRoot\fast1-1088.flags","$fixtureRoot\fixture\processing1088.rgba32f","$fixtureRoot\fixture\valid.rgba32f")){$identity+=@{path=$path;sha256=(Get-FileHash $path).Hash;bytes=(Get-Item $path).Length}}
 $identity|ConvertTo-Json -Depth 4|Set-Content "$r\identity.json"
 $i=0;$raw=@()
 # Two explicit 4-slot paired comparisons per round; no adaptive re-runs.
 foreach($round in 1..3){foreach($pair in @('current-v-mochi')){
  $schedule=if($pair -eq 'current-v-mochi'){@('N','M','M','N')}else{@('O','N','N','O')}
  foreach($kind in $schedule){
   CheckGame;if((Get-PSDrive D).Free -lt 100GB){throw 'D free<100GB during run'}
   $label=('{0:D2}-r{1}-{2}-{3}' -f $i,$round,$pair,$kind);$out="$r\$label";if(Test-Path $out){throw "Output exists $label; do not overwrite batch"};New-Item -ItemType Directory $out|Out-Null
   $psi=New-Object Diagnostics.ProcessStartInfo;$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true;$psi.WorkingDirectory=$r
   foreach($key in @($psi.EnvironmentVariables.Keys)){if($key.StartsWith('DLSS5_') -or $key.StartsWith('NR_')){$psi.EnvironmentVariables.Remove($key)}}
   $psi.EnvironmentVariables['TEMP']="$r\cache";$psi.EnvironmentVariables['TMP']="$r\cache";$psi.EnvironmentVariables['HIP_CACHE_DIR']="$r\cache"
   if($kind -eq 'M'){$psi.FileName="$mz\nr_graph.exe";$psi.Arguments="--plan $mz\plan-1920x1080.txt --model-pack $mz\dlssnr.bin --spv-dir $mz\spv --host-boundary --reuse --source-width 1920 --source-height 1080 --accumulation fp32 --pipeline-cache $r\mochi-cache.bin --warmup 80 --repeats 160 --chunk 1 --style 1 --img-seed 0 --in-image $fixtureRoot\fixture\valid.rgba32f --out-image $out\final.rgba32f"}
   else {$mod=if($kind -eq 'O'){$oldModules}else{$modules};$psi.FileName=if($kind -eq 'O'){'D:\DLSSNR-Lab\fresh-mochi1088-20261006\benchmark.exe'}else{"$r\benchmark.exe"};$psi.Arguments="$assets $mod $fixtureRoot\fast1-1088.flags $fixtureRoot\fixture\processing1088.rgba32f $out 80 160 1088"}
   @{label=$label;round=$round;pair=$pair;kind=$kind;exe=$psi.FileName;arguments=$psi.Arguments;started=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content "$out\command.json"
   $p=New-Object Diagnostics.Process;$p.StartInfo=$psi;if(!$p.Start()){throw 'Start failed'};$std=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync();$start=[DateTime]::UtcNow;$next=$start.AddSeconds(15)
   try {while(!$p.HasExited){Start-Sleep -Milliseconds 100;if([DateTime]::UtcNow -ge $next){CheckGame;$next=[DateTime]::UtcNow.AddSeconds(15)};if(([DateTime]::UtcNow-$start).TotalSeconds -gt 120){throw 'Own probe timeout'}}} finally {if(!$p.HasExited){$p.Kill()};$p.WaitForExit();$std.Result|Set-Content "$out\stdout.log";$err.Result|Set-Content "$out\stderr.log"}
   if($p.ExitCode -ne 0){throw "Probe failed $label exit=$($p.ExitCode)"}
   foreach($f in Get-ChildItem $out -File | Where-Object {$_.Name -match '\.(rgb32f|rgba32f)$'}){$raw+=@{slot=$label;file=$f.Name;bytes=$f.Length;sha256=(Get-FileHash $f.FullName).Hash}}
   $raw|ConvertTo-Json -Depth 4|Set-Content "$r\raw-hashes.json"
   Write-Output $label;Get-Content "$out\stdout.log"|Select-String 'SYNC_NETWORK|SYNC_PROFILE|CURRENT_PROFILE|measurement wall|working extent|one submit'
   $i++
  }
 }}
 $after=@(Get-ChildItem $mz -File -Recurse | ForEach-Object { $_.FullName+'|'+$_.Length+'|'+$_.LastWriteTimeUtc.Ticks});$delta=@(Compare-Object $before $after)
 @{pass=($delta.Count -eq 0);changes=$delta}|ConvertTo-Json -Depth 4|Set-Content "$r\old-assets-unmodified.json";if($delta.Count){throw 'Old mochi tree changed'}
 'COMPLETED_12_SLOTS'
}finally{if($p -and !$p.HasExited){$p.Kill()};$owner.Dispose();Remove-Item $lock -Force;'LOCK_RELEASED'}
