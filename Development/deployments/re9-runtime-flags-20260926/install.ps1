param([Parameter(Mandatory=$true)][string]$GameDir,[string]$RestoreBackup='')
# RE9 package: configurable runtime (reads DLSS5_HIP_* / DLSS5_SKIP_BLOCKS / DLSS5_FIT_LARGE / DLSS5_NETWORK_HEIGHT from
# DLSS5-AMD\native-game-flags.txt) with the 0.31 kernel groups on by default, the vec-input c32-wave1, the shared-buffer
# pool and PR #9. Replaces LmxxfNrRuntime.dll (+ _storage_ copy), the two c32-wave1.hsaco (+ HIP\SHA256SUMS lines) and
# amends the flags file (NETWORK_HEIGHT=auto; appends the four kernel keys only if absent). -RestoreBackup <dir> undoes it.
# PREPARED, NOT EXECUTED. Run with the game closed, from this folder (payload\ beside it).
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$assets="$GameDir\DLSS5-AMD\native-game-tiled-assets";$hip="$assets\HIP";$flags="$GameDir\DLSS5-AMD\native-game-flags.txt"
$payload=Get-Content "$root\payload.json" -Raw|ConvertFrom-Json
if(Get-Process re9,re9demo,pragmata -ErrorAction SilentlyContinue){throw 'game running'}
if(!(Test-Path "$GameDir\LmxxfNrRuntime.dll") -or !(Test-Path "$hip\SHA256SUMS")){throw 'not an RE9 package install (LmxxfNrRuntime.dll / HIP\SHA256SUMS missing)'}
$targets=@('LmxxfNrRuntime.dll')+@(if(Test-Path "$GameDir\_storage_\LmxxfNrRuntime.dll"){'_storage_\LmxxfNrRuntime.dll'})
function Key($t){$t -replace '\\','_'}
if($RestoreBackup){
  foreach($t in $targets){Copy-Item "$RestoreBackup\$(Key $t)" "$GameDir\$t" -Force}
  foreach($a in 'gfx1200','gfx1201'){if(Test-Path "$RestoreBackup\c32-wave1-$a.hsaco"){Copy-Item "$RestoreBackup\c32-wave1-$a.hsaco" "$hip\$a\c32-wave1.hsaco" -Force}}
  Copy-Item "$RestoreBackup\SHA256SUMS" "$hip\SHA256SUMS" -Force;Copy-Item "$RestoreBackup\native-game-flags.txt" $flags -Force
  "RESTORED $RestoreBackup";exit
}
foreach($p in $payload){if((Get-FileHash "$root\payload\$($p.file)").Hash -ne $p.sha256){throw "payload hash mismatch $($p.file)"}}
$b="$root\backups\$(Get-Date -Format yyyyMMdd-HHmmss)";New-Item -ItemType Directory -Force $b|Out-Null
foreach($t in $targets){Copy-Item "$GameDir\$t" "$b\$(Key $t)"}
foreach($a in 'gfx1200','gfx1201'){if(Test-Path "$hip\$a\c32-wave1.hsaco"){Copy-Item "$hip\$a\c32-wave1.hsaco" "$b\c32-wave1-$a.hsaco"}}
Copy-Item "$hip\SHA256SUMS" "$b\SHA256SUMS";Copy-Item $flags "$b\native-game-flags.txt"
try{
  $rt=($payload|Where-Object file -eq 'LmxxfNrRuntime.dll')
  foreach($t in $targets){Copy-Item "$root\payload\LmxxfNrRuntime.dll" "$GameDir\$t" -Force;if((Get-FileHash "$GameDir\$t").Hash -ne $rt.sha256){throw "verify $t"}}
  $sums=[IO.File]::ReadAllLines("$hip\SHA256SUMS")
  foreach($a in 'gfx1200','gfx1201'){
    if(!(Test-Path "$hip\$a")){continue}
    $p=$payload|Where-Object file -eq "c32-wave1-$a.hsaco"
    Copy-Item "$root\payload\c32-wave1-$a.hsaco" "$hip\$a\c32-wave1.hsaco" -Force
    $line="$($p.sha256.ToLower())  $a/c32-wave1.hsaco"
    if($sums -match "  $a/c32-wave1\.hsaco$"){$sums=$sums|ForEach-Object{if($_ -match "  $a/c32-wave1\.hsaco$"){$line}else{$_}}}else{$sums+=$line}
  }
  [IO.File]::WriteAllLines("$hip\SHA256SUMS",$sums,(New-Object Text.UTF8Encoding($false)))
  $lines=@(Get-Content $flags)|ForEach-Object{if($_ -match '^\s*DLSS5_NETWORK_HEIGHT\s*='){'DLSS5_NETWORK_HEIGHT=auto'}else{$_}}
  foreach($k in 'DLSS5_HIP_WAVE_OWNED=1','DLSS5_HIP_C512_M32=1','DLSS5_HIP_VIT_PROJ_N64=1','DLSS5_HIP_PDL=1'){$name=$k.Split('=')[0];if(!($lines -match "^\s*$name\s*=")){$lines+=$k}}
  [IO.File]::WriteAllLines($flags,$lines)
  "INSTALLED runtime ($($targets.Count) copies) + c32-wave1 + flags amended; BACKUP=$b"
}catch{& $MyInvocation.MyCommand.Path -GameDir $GameDir -RestoreBackup $b;throw}
