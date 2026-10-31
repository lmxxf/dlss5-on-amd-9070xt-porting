$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$root='D:\DLSSNR-Lab\game-install-20261031';$release='D:\DLSSNR-Lab\release-046'
$games=@('C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64','C:\XboxGames\Onimusha- Way of the Sword\Content')
$baseNames=@('OptiScaler-DLSS5-AMD-0.45','OptiScaler-REFramework-DLSS5-AMD-0.45')
$baseHashes=@('96c12292cb11c8b7da026c012166745e0ca330f86d9ff16267a55163d9c367c5','a46fee43bfc67e8fe367e3a4593ddd71dfbcd8afb9c91c330effa012e8857604')
$utf8=[Text.UTF8Encoding]::new($false);$held=$null;$child=$null;$manifest=@();$plans=@();$protected=@();$mutated=$false;$next=[DateTime]::MinValue
function Hash($p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLower()}
function Save($p,$o){[IO.File]::WriteAllText($p,($o|ConvertTo-Json -Depth 10),$utf8)}
function Idle {& D:\DLSSNR-Lab\game-check.ps1 'Shipping SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game-check not idle'};foreach($d in 'C','D'){if((Get-PSDrive $d).Free -lt 100GB){throw "$d free below100GB"}}}
function Watch {if([DateTime]::UtcNow -ge $script:next){Idle;$script:next=[DateTime]::UtcNow.AddSeconds(15)}}
function Configs {
 $a=@();foreach($i in 0,1){$g=$games[$i];$paths=@("$g\DLSS5-AMD\default-config.txt","$g\DLSS5-AMD\custom-config.txt","$g\DLSS5-AMD\native-game-flags.txt")
 foreach($dir in @($g,"$g\_storage_")){if(Test-Path $dir){$paths+=@(Get-ChildItem -LiteralPath $dir -File|Where-Object {$_.Extension -in '.ini','.cfg'}|ForEach-Object {$_.FullName})}}
 foreach($f in $paths|Sort-Object -Unique){$ex=Test-Path -LiteralPath $f;$a+=[pscustomobject]@{game=$i;path=$f;existed=$ex;sha256=$(if($ex){Hash $f})}}};return $a
}
function CheckConfigs {foreach($c in $script:protected){if((Test-Path -LiteralPath $c.path) -ne $c.existed){throw "configuration existence changed $($c.path)"};if($c.existed -and (Hash $c.path) -ne $c.sha256){throw "configuration bytes changed $($c.path)"}}}
function Restore {Idle;foreach($m in $script:manifest){Watch;New-Item -ItemType Directory -Force (Split-Path $m.path)|Out-Null;Copy-Item -LiteralPath $m.backup -Destination $m.path -Force;if((Hash $m.path) -ne $m.sha256){throw "rollback hash $($m.path)"}};foreach($plan in $script:plans){foreach($item in $plan.items){if(!$item.existed -and (Test-Path -LiteralPath $item.destination)){Remove-Item -LiteralPath $item.destination -Force}}};CheckConfigs}
try {
 Idle;if(Test-Path "$root\receipt.json"){throw 'completed install exists'}
 if(@(Get-Process|Where-Object {$_.ProcessName -match '^(frame|pure|benchmark|rtc_compile.*|runtime-smoke)$'}).Count){throw 'experiment active'}
 $held=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);Idle
 New-Item -ItemType Directory -Force $root|Out-Null
 foreach($f in (Get-Content "$release\payload-expected.json" -Raw -Encoding UTF8|ConvertFrom-Json)){Watch;if((Hash (Join-Path $release $f.relative)) -ne $f.sha256){throw "payload identity $($f.relative)"}}
 if(@(Get-ChildItem "$release\HIP" -Recurse -Filter *.hsaco).Count -ne 80){throw 'payload module count'}
 if((Hash "$release\payload\runtime-smoke.exe") -ne '2ffe128b6d1c24b1aab33f851ee47d2b2abe62fce10364bd628fc3ca23f13014'){throw 'smoke identity'}
 $protected=Configs;if($protected.Count -ne 16){throw 'Expected16 protected configurations'};Save "$root\config-before.json" $protected
 $backup="$root\backup-$(Get-Date -Format yyyyMMdd-HHmmss)";New-Item -ItemType Directory -Force $backup|Out-Null
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 foreach($i in 0,1){
  $g=$games[$i];$relset=@{};$items=@();$zip="D:\給網友打包\$($baseNames[$i]).zip";if(!(Test-Path -LiteralPath $zip)){$zip="D:\給網友打包\history\$($baseNames[$i]).zip"}
  if((Hash $zip) -ne $baseHashes[$i]){throw '0.45 base changed'}
  $z=[IO.Compression.ZipFile]::OpenRead($zip);try{foreach($e in $z.Entries){if(!$e.Name){continue};$n=$e.FullName.Replace('/','\');if($n.Contains('..') -or [IO.Path]::IsPathRooted($n)){throw 'unsafe baseline entry'};if(Test-Path -LiteralPath (Join-Path $g $n) -PathType Leaf){$relset[$n]=$true}}}finally{$z.Dispose()}
  foreach($dir in 'DLSS5-AMD','shaders','_storage_'){if(Test-Path -LiteralPath "$g\$dir"){foreach($f in Get-ChildItem -LiteralPath "$g\$dir" -File -Recurse){$relset[$f.FullName.Substring($g.Length+1)]=$true}}}
  foreach($c in $protected|Where-Object {$_.game -eq $i -and $_.existed}){$relset[$c.path.Substring($g.Length+1)]=$true}
  $map=@{};if($i -eq 0){$map['dlss5-amd.addon64']="$release\payload\dlss5-amd.addon64"}else{$map['LmxxfNrRuntime.dll']="$release\payload\LmxxfNrRuntime.dll";$map['_storage_\LmxxfNrRuntime.dll']="$release\payload\LmxxfNrRuntime.dll"}
  foreach($f in Get-ChildItem "$release\HIP" -File -Recurse){$map['DLSS5-AMD\native-game-tiled-assets\HIP\'+$f.FullName.Substring(("$release\HIP\").Length)]=$f.FullName}
  foreach($n in $map.Keys){$dst=Join-Path $g $n;$exists=Test-Path -LiteralPath $dst;if($exists){$relset[$n]=$true};$items+=@{relative=$n;source=$map[$n];destination=$dst;existed=$exists;sha256=(Hash $map[$n])}}
  foreach($n in $relset.Keys|Sort-Object){Watch;$f=Join-Path $g $n;$b=Join-Path "$backup\game-$i" $n;$sha=Hash $f;New-Item -ItemType Directory -Force (Split-Path $b)|Out-Null;Copy-Item -LiteralPath $f -Destination $b -Force;if((Hash $b) -ne $sha){throw "backup mismatch $f"};$manifest+=@{game=$i;relative=$n;path=$f;backup=$b;sha256=$sha;bytes=(Get-Item -LiteralPath $f).Length}}
  $plans+=@{game=$i;items=$items}
 }
 Save "$root\backup-manifest.json" $manifest;Save "$root\install-plan.json" $plans
 # Complete rollback exists BEFORE the first mutation; user config bytes are never rewritten by this installer.
 $rb=@'
$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\game-install-20261031';$held=$null;$next=[DateTime]::MinValue
function Idle {& D:\DLSSNR-Lab\game-check.ps1 'Shipping SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'exit games first'};foreach($d in 'C','D'){if((Get-PSDrive $d).Free -lt 100GB){throw 'disk below100GB'}}}
Idle;$held=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{Idle;foreach($m in (Get-Content "$r\backup-manifest.json" -Raw -Encoding UTF8|ConvertFrom-Json)){if([DateTime]::UtcNow -ge $next){Idle;$next=[DateTime]::UtcNow.AddSeconds(15)};New-Item -ItemType Directory -Force (Split-Path $m.path)|Out-Null;Copy-Item -LiteralPath $m.backup -Destination $m.path -Force;if((Get-FileHash -LiteralPath $m.path).Hash.ToLower() -ne $m.sha256){throw 'rollback readback mismatch'}};foreach($plan in (Get-Content "$r\install-plan.json" -Raw -Encoding UTF8|ConvertFrom-Json)){foreach($i in $plan.items){if(!$i.existed){Remove-Item -LiteralPath $i.destination -Force -ErrorAction SilentlyContinue}}};'ROLLBACK_COMPLETE'}finally{if($held){$held.Dispose();Remove-Item D:\DLSSNR-Lab\gpu.lock}}
'@
 [IO.File]::WriteAllText("$root\rollback.ps1",$rb,$utf8);CheckConfigs
 try{
  $installed=@();$mutated=$true
  foreach($plan in $plans){foreach($i in $plan.items){Watch;$dst=$i.destination;New-Item -ItemType Directory -Force (Split-Path $dst)|Out-Null;$tmp=$dst+'.046-'+[Guid]::NewGuid().ToString('N');$previous=$tmp+'.previous'
   try{Copy-Item -LiteralPath $i.source -Destination $tmp;if((Hash $tmp) -ne $i.sha256){throw 'temporary copy mismatch'};if(Test-Path -LiteralPath $dst){[IO.File]::Replace($tmp,$dst,$previous)}else{[IO.File]::Move($tmp,$dst)}}finally{if(Test-Path -LiteralPath $tmp){Remove-Item -LiteralPath $tmp -Force};if(Test-Path -LiteralPath $previous){Remove-Item -LiteralPath $previous -Force}}
   if((Hash $dst) -ne $i.sha256){throw 'installed hash mismatch'};$installed+=@{game=$plan.game;relative=$i.relative;sha256=$i.sha256}
  };if(@(Get-ChildItem "$($games[$plan.game])\DLSS5-AMD\native-game-tiled-assets\HIP" -Recurse -Filter *.hsaco).Count -ne 80){throw 'installed module count'}}
  if($installed.Count -ne 169){throw 'Expected169 installed files'};CheckConfigs;$smokes=@();$g=$games[1]
  foreach($where in @('root','storage')){
   Idle;$dll=if($where -eq 'root'){"$g\LmxxfNrRuntime.dll"}else{"$g\_storage_\LmxxfNrRuntime.dll"}
   $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName="$release\payload\runtime-smoke.exe";$si.Arguments="`"$dll`" `"$g\DLSS5-AMD\native-game-tiled-assets\HIP`" 32";$si.WorkingDirectory=$root;$si.UseShellExecute=$false;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true;$si.EnvironmentVariables['LMXXF_WEIGHTS_DIR']="$g\DLSS5-AMD\native-game-tiled-assets";$si.EnvironmentVariables['LMXXF_SHADER_DIR']="$g\DLSS5-AMD\native-game-tiled-assets"
   $child=[Diagnostics.Process]::new();$child.StartInfo=$si;if(!$child.Start()){throw 'smoke start'};$o=$child.StandardOutput.ReadToEndAsync();$e=$child.StandardError.ReadToEndAsync();$start=[DateTime]::UtcNow
   try{while(!$child.HasExited){Start-Sleep -Milliseconds 100;Watch;if(([DateTime]::UtcNow-$start).TotalSeconds -gt 15){throw 'smoke15s watchdog'}};$child.WaitForExit()}finally{if(!$child.HasExited){$child.Kill();$child.WaitForExit()};[IO.File]::WriteAllText("$root\smoke-$where.stdout.log",$o.Result,$utf8);[IO.File]::WriteAllText("$root\smoke-$where.stderr.log",$e.Result,$utf8)}
   if($child.ExitCode -ne 0){throw "smoke $where exit=$($child.ExitCode)"};$smokes+=@{route=$where;dll=$dll;sha256=(Hash $dll);exit_code=$child.ExitCode};CheckConfigs
  }
  foreach($plan in $plans){foreach($i in $plan.items){Watch;if((Hash $i.destination) -ne $i.sha256){throw 'final installed readback'}}}
  CheckConfigs;Save "$root\config-after.json" (Configs)
  Save "$root\receipt.json" ([ordered]@{version='0.46';payload_validation='yami-round5-20261031/final-decision.json';games=$games;backup=$backup;backup_files=$manifest.Count;backup_bytes=($manifest|ForEach-Object {[long]$_.bytes}|Measure-Object -Sum).Sum;rollback="$root\rollback.ps1";installed=$installed;protected_configs=$protected;all_configuration_bytes_unchanged=$true;runtime_smokes=$smokes;modules_per_game=80;utc=[DateTime]::UtcNow.ToString('o')})
  'INSTALL_046_COMPLETE'
 }catch{$reason=$_;try{Restore;Save "$root\failure.json" @{reason="$reason";restored=$true}}catch{Save "$root\failure.json" @{reason="$reason";restore_error="$_";rollback="$root\rollback.ps1"}};throw $reason}
}finally{if($child -and !$child.HasExited){$child.Kill()};if($held){$held.Dispose();Remove-Item D:\DLSSNR-Lab\gpu.lock};'LOCK_RELEASED'}
