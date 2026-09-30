# Stellar Blade: swap the given modules (both arches) only; add-on, runtime and flags untouched. Onimusha: runtime (Content + _storage_) backed up, not replaced; HIP modules mirrored from Stellar Blade.
param([string]$RestoreStellar='',[string]$RestoreOni='',[string[]]$Modules=@('deep_fast-packed'))
$ErrorActionPreference='Stop'
$root='D:\DLSSNR-Lab\hip-backend\vit-1080-gap-20261001'
$game='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
$oni='C:\XboxGames\Onimusha- Way of the Sword\Content'
function Idle {if(Get-Process|Where-Object{$_.ProcessName -match 'Shipping|^re9$|^Onimusha|^SandFall|^benchmark|^rt_bench|^jobbench|^rtc_compile|^runtime-smoke|^Magpie'}){throw 'Game/GPU lab busy'}}
function Restore($base,$backup){$m=Get-Content "$backup\backup.json" -Raw|ConvertFrom-Json;foreach($f in $m){$dst=Join-Path $base $f.relative;Copy-Item (Join-Path $backup $f.relative) $dst -Force;if((Get-FileHash $dst).Hash -ne $f.sha256){throw 'Restore hash mismatch'}}}
function Backup($base,$rels,$backup){New-Item -ItemType Directory -Force $backup|Out-Null;$saved=@();foreach($rel in $rels){$src=Join-Path $base $rel;$dst=Join-Path $backup $rel;New-Item -ItemType Directory -Force (Split-Path -Parent $dst)|Out-Null;Copy-Item $src $dst;$sha=(Get-FileHash $src).Hash;if((Get-FileHash $dst).Hash -ne $sha){throw 'Backup hash mismatch'};$saved+=@{relative=$rel;sha256=$sha}};$saved|ConvertTo-Json -Depth 4|Set-Content "$backup\backup.json"}
function Sums($hip){$u=New-Object Text.UTF8Encoding($false);$lines=@(Get-ChildItem $hip -Recurse -Filter '*.hsaco'|Sort-Object FullName|ForEach-Object{$rel=$_.FullName.Substring($hip.Length+1).Replace('\','/');"$((Get-FileHash $_.FullName).Hash.ToLower())  $rel"});[IO.File]::WriteAllLines("$hip\SHA256SUMS",$lines,$u);$lines.Count}
Idle
if($RestoreStellar){Restore $game $RestoreStellar;'RESTORED STELLAR';exit 0}
if($RestoreOni){Restore $oni $RestoreOni;'RESTORED ONI';exit 0}
$stamp=Get-Date -Format yyyyMMdd-HHmmss
# ---- Stellar Blade
$snapshot=Get-Content "$root\snapshot.json" -Raw|ConvertFrom-Json
foreach($f in $snapshot){if((Get-FileHash $f.path).Hash -ne $f.sha256){throw "Installed baseline changed: $($f.path)"}}
$flags=Get-Content "$game\DLSS5-AMD\native-game-flags.txt" -Raw
foreach($e in 'DLSS5_DIRECT_IO=3','DLSS5_MAKE_RESIDENT_EVERY=60','DLSS5_HIP_SWIN_RUN=1'){if($flags -notmatch "(?m)^\s*$e\s*$"){throw "Flag missing: $e"}}
$hipRel='DLSS5-AMD\native-game-tiled-assets\HIP'
$mods=@($Modules|ForEach-Object{$_ -split ','}|Where-Object{$_}|ForEach-Object{"$_.hsaco"})
$rels=@("$hipRel\SHA256SUMS")+@(foreach($a in 'gfx1200','gfx1201'){foreach($m in $mods){"$hipRel\$a\$m"}})
$sb="$root\backups\stellar-$stamp-map3";Backup $game $rels $sb
try{Idle
 foreach($a in 'gfx1200','gfx1201'){foreach($m in $mods){Copy-Item "$root\build-final\$a\$m" "$game\$hipRel\$a\$m" -Force;if((Get-FileHash "$game\$hipRel\$a\$m").Hash -ne (Get-FileHash "$root\build-final\$a\$m").Hash){throw 'module readback'}}}
 if((Sums "$game\$hipRel") -ne 62){throw 'Expected 62 modules'}
 foreach($f in $snapshot){if($mods|Where-Object{$f.path -like "*\$_"}){continue};if((Get-FileHash $f.path).Hash -ne $f.sha256){throw "Protected file changed $($f.path)"}}
 if((Get-Content "$game\DLSS5-AMD\native-game-flags.txt" -Raw) -cne $flags){throw 'flags changed'}
}catch{Restore $game $sb;throw}
"STELLAR backup=$sb addon=$((Get-FileHash "$game\dlss5-amd.addon64").Hash)"
# ---- Onimusha
$ob="D:\DLSSNR-Lab\onimusha-backups\$stamp-map3"
$orels=@('LmxxfNrRuntime.dll')+$(if(Test-Path "$oni\_storage_\LmxxfNrRuntime.dll"){@('_storage_\LmxxfNrRuntime.dll')}else{@()})
$orels+=@(Get-ChildItem "$oni\$hipRel" -Recurse -File|ForEach-Object{$_.FullName.Substring($oni.Length+1)})
Backup $oni $orels $ob
try{Idle
 foreach($a in 'gfx1200','gfx1201'){Get-ChildItem "$oni\$hipRel\$a" -Filter '*.hsaco'|Where-Object{!(Test-Path "$game\$hipRel\$a\$($_.Name)")}|ForEach-Object{throw "Onimusha-only module $($_.Name)"};Copy-Item "$game\$hipRel\$a\*.hsaco" "$oni\$hipRel\$a" -Force}
 Copy-Item "$game\$hipRel\SHA256SUMS" "$oni\$hipRel\SHA256SUMS" -Force
 foreach($f in Get-ChildItem "$game\$hipRel" -Recurse -File){$o=$f.FullName.Replace($game,$oni);if((Get-FileHash $o).Hash -ne (Get-FileHash $f.FullName).Hash){throw "Onimusha module mismatch $o"}}
}catch{Restore $oni $ob;throw}
"ONI backup=$ob runtime=$((Get-FileHash "$oni\LmxxfNrRuntime.dll").Hash) files=$($orels.Count)"
