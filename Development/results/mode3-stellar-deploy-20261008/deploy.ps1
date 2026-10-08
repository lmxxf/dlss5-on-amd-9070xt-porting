param([switch]$Apply,[string]$Addon,[string]$AddonSHA,[string]$Runtime,[string]$RuntimeSHA)
$ErrorActionPreference='Stop'
$roots=@('C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64')
$targets=@(@{source=$Addon;sha=$AddonSHA;dest=(Join-Path $roots[0] 'dlss5-amd.addon64')})
$keys=@{DLSS5_TEMPORAL_MODE='3';DLSS5_TEMPORAL_ENHANCE_STRENGTH='1';DLSS5_FAST_HISTORY='0';DLSS5_TEMPORAL_HISTORY_EXPERIMENT='0';DLSS5_MULTI_PASS='1';DLSS5_MULTI_PASS_PREDICT='0'}
function Idle {& D:\DLSSNR-Lab\game-check.ps1 'SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game busy/check failed'};if(Get-Process|Where-Object{$_.ProcessName -match 'Shipping|SB-Win64|Onimusha|^re9$|Magpie|rtc_compile'}){throw 'game/compiler active'}}
function EditConfig($path,$native){
 [byte[]]$bytes=@();if(Test-Path $path){$bytes=[IO.File]::ReadAllBytes($path)};$bom=$bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191
 $off=if($bom){3}else{0};$text=[Text.Encoding]::UTF8.GetString($bytes,$off,$bytes.Length-$off);$nl=if($text.Contains("`r`n")){"`r`n"}else{"`n"}
 foreach($k in $keys.Keys){$pattern='(?m)^'+[regex]::Escape($k)+'=[^\r\n]*';$has=[regex]::IsMatch($text,$pattern);$value="$k=$($keys[$k])";if($has){$text=[regex]::Replace($text,$pattern,[Text.RegularExpressions.MatchEvaluator]{param($m)$value})}elseif(!$native){if($text.Length -and !$text.EndsWith("`n")){$text+=$nl};$text+=$value+$nl}}
 $enc=[Text.UTF8Encoding]::new($bom);[IO.File]::WriteAllText($path,$text,$enc)
}
if(!$Apply){'PLAN ONLY NO CHANGES';$targets|ConvertTo-Json;$keys|ConvertTo-Json;'Only Stellar addon/config changed; preserve all modules/row/SUMS, AE/SKIN/strength/geometry/MV/depth/default; Onimusha untouched';return}
Idle;if([IO.DriveInfo]::new('D:\').AvailableFreeSpace -lt 100GB){throw 'D free<100GB'}
foreach($t in $targets){if(!$t.source -or !$t.sha -or !(Test-Path $t.source) -or (Get-FileHash $t.source).Hash -ne $t.sha){throw 'candidate identity mismatch'}}
foreach($tier in 'User','Machine','Process'){$envs=[Environment]::GetEnvironmentVariables($tier);foreach($k in $keys.Keys){if($envs.Contains($k) -and $envs[$k] -ne $keys[$k]){throw "environment override $tier $k; no config writes"}}}
$owner=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {
 Idle;$backup='D:\DLSSNR-Lab\mode3-stellar-deploy-20261008\backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss');New-Item -ItemType Directory $backup|Out-Null
 $files=@($targets|ForEach-Object{$_.dest});foreach($r in $roots){foreach($n in 'default-config.txt','custom-config.txt','native-game-flags.txt'){$files+=Join-Path $r ('DLSS5-AMD\'+$n)}}
 $manifest=@();$i=0;foreach($p in $files){$present=Test-Path $p;$copy=Join-Path $backup "file-$i";$hash=$null;if($present){Copy-Item $p $copy;$hash=(Get-FileHash $p).Hash};$manifest+=@{path=$p;present=$present;backup=$copy;sha=$hash};$i++}
 $manifest|ConvertTo-Json -Depth 4|Set-Content "$backup\manifest.json" -Encoding UTF8
 $moduleManifest=@();foreach($r in $roots){$hip=Join-Path $r 'DLSS5-AMD\native-game-tiled-assets\HIP';foreach($f in Get-ChildItem $hip -File -Recurse|Where-Object{$_.Extension -eq '.hsaco' -or $_.Name -eq 'SHA256SUMS'}|Sort-Object FullName){$moduleManifest+=@{path=$f.FullName;sha=(Get-FileHash $f.FullName).Hash}}};foreach($r in $roots){$p=Join-Path $r 'DLSS5-AMD\native-game-tiled-assets\post70-history-head.f16';if(Test-Path $p){$moduleManifest+=@{path=$p;sha=(Get-FileHash $p).Hash}}};$moduleManifest|ConvertTo-Json|Set-Content "$backup\modules-readonly.json"
 @('$ErrorActionPreference=''Stop''','& D:\DLSSNR-Lab\game-check.ps1 ''SB-Win64 Onimusha Magpie''','if($LASTEXITCODE -ne 1){throw ''game busy''}',('$rows=Get-Content ''{0}\manifest.json'' -Raw|ConvertFrom-Json' -f $backup),'$lease=[IO.File]::Open(''D:\DLSSNR-Lab\gpu.lock'',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)', 'try{foreach($r in $rows){if($r.present){Copy-Item $r.backup $r.path -Force;if((Get-FileHash $r.path).Hash -ne $r.sha){throw ''restore hash''}}elseif(Test-Path $r.path){Remove-Item $r.path}}}finally{$lease.Dispose();Remove-Item ''D:\DLSSNR-Lab\gpu.lock''}')|Set-Content "$backup\rollback.ps1"
 try {Idle;foreach($t in $targets){Copy-Item $t.source $t.dest -Force;if((Get-FileHash $t.dest).Hash -ne $t.sha){throw 'payload readback'}};foreach($r in $roots){EditConfig (Join-Path $r 'DLSS5-AMD\custom-config.txt') $false;EditConfig (Join-Path $r 'DLSS5-AMD\native-game-flags.txt') $true};$after=@();foreach($p in $files){if(Test-Path $p){$after+=@{path=$p;sha=(Get-FileHash $p).Hash}}};foreach($m in $moduleManifest){if((Get-FileHash $m.path).Hash -ne $m.sha){throw 'untouched module identity changed'}};$after|ConvertTo-Json|Set-Content "$backup\after.json";"DEPLOYED backup=$backup restart_required no_game_started"}
 catch {foreach($r in $manifest){if($r.present){Copy-Item $r.backup $r.path -Force}else{Remove-Item $r.path -ErrorAction SilentlyContinue}};throw}
} finally {$owner.Dispose();Remove-Item 'D:\DLSSNR-Lab\gpu.lock';'LOCK_RELEASED'}
