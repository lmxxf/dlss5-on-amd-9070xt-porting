$ErrorActionPreference='Stop'
$s='D:\給網友打包\OptiScaler-DLSS5-AMD-0.46'
$g='C:\Program Files (x86)\Steam\steamapps\common\PBZ\platform\steam'
$r='D:\DLSSNR-Lab\pbz-install-20261108'
function Idle {
 & D:\DLSSNR-Lab\game-check.ps1 'PBZ SB-Win64 Onimusha Magpie'
 if($LASTEXITCODE -ne 1){throw 'Game active or game-check failed'}
 if(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -match '^(frame|pure|benchmark|rtc_compile|final-profile|runtime-smoke)$'}){throw 'GPU tool active'}
}
Idle
if((Get-PSDrive D).Free -lt 100GB -or (Get-PSDrive C).Free -lt 5GB){throw 'Insufficient disk space'}
if((Get-Item "$g\PBZ.exe").Length -lt 400MB){throw 'Wrong executable'}
if(Test-Path "$r\receipt.json"){throw 'Already installed; inspect receipt'}
$held=$null;$plan=@();$mutated=$false
try {
 $held=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 Idle
 if((Get-FileHash "$s.zip").Hash.ToLower() -ne 'ead95c097b2e3f399bd39bfe5565e524e8cea2a0ca939a4a751910d94148de94'){throw 'Release ZIP SHA mismatch'}
 $lines=@(Get-Content "$s\SHA256SUMS.txt" -Encoding UTF8)
 $next=[DateTime]::UtcNow
 foreach($line in $lines){
  if(!$line.Trim()){continue}
  if($line -notmatch '^([0-9a-fA-F]{64})\s+\*?(.+)$'){throw "Invalid manifest line: $line"}
  $sha=$matches[1].ToLower();$rel=$matches[2].Replace('/','\')
  if($rel -match '(^[\\/]|:|(^|\\)\.\.(\\|$))'){throw 'Unsafe path'}
  if([DateTime]::UtcNow -ge $next){Idle;$next=[DateTime]::UtcNow.AddSeconds(15)}
  if((Get-FileHash -LiteralPath "$s\$rel").Hash.ToLower() -ne $sha){throw "Source mismatch $rel"}
  $dst="$g\$rel";$exists=Test-Path -LiteralPath $dst
  $old=$null;if($exists){$old=(Get-FileHash -LiteralPath $dst).Hash.ToLower()}
  $plan+= [pscustomobject]@{relative=$rel;destination=$dst;sha256=$sha;existed=$exists;oldsha=$old;backup="$r\backup\$rel"}
 }
 # Include the checksum manifest itself.
 $plan+=[pscustomobject]@{relative='SHA256SUMS.txt';destination="$g\SHA256SUMS.txt";sha256=(Get-FileHash "$s\SHA256SUMS.txt").Hash.ToLower();existed=(Test-Path "$g\SHA256SUMS.txt");oldsha=$null;backup="$r\backup\SHA256SUMS.txt"}
 New-Item -ItemType Directory -Force $r | Out-Null
 foreach($p in $plan){if($p.existed){if(!$p.oldsha){$p.oldsha=(Get-FileHash $p.destination).Hash.ToLower()};New-Item -ItemType Directory -Force (Split-Path $p.backup)|Out-Null;Copy-Item -LiteralPath $p.destination -Destination $p.backup;if((Get-FileHash $p.backup).Hash.ToLower() -ne $p.oldsha){throw 'Backup mismatch'}}}
 $plan | ConvertTo-Json -Depth 5 | Set-Content "$r\plan.json" -Encoding UTF8
 @'
$ErrorActionPreference='Stop'
$r=$PSScriptRoot
& D:\DLSSNR-Lab\game-check.ps1 'PBZ SB-Win64 Onimusha Magpie'
if($LASTEXITCODE -ne 1){throw 'Game active/check failed'}
$held=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {foreach($p in (Get-Content "$r\plan.json" -Raw -Encoding UTF8|ConvertFrom-Json)){if($p.existed){Copy-Item -LiteralPath $p.backup -Destination $p.destination -Force;if((Get-FileHash $p.destination).Hash.ToLower() -ne $p.oldsha){throw 'Restore mismatch'}}else{Remove-Item -LiteralPath $p.destination -Force -ErrorAction SilentlyContinue}};'ROLLBACK_COMPLETE'}finally{$held.Dispose();Remove-Item D:\DLSSNR-Lab\gpu.lock}
'@ | Set-Content "$r\rollback.ps1" -Encoding UTF8
 $exeSha=(Get-FileHash "$g\PBZ.exe").Hash
 Idle;$mutated=$true;$next=[DateTime]::UtcNow
 foreach($p in $plan){if([DateTime]::UtcNow -ge $next){Idle;$next=[DateTime]::UtcNow.AddSeconds(15)};New-Item -ItemType Directory -Force (Split-Path $p.destination)|Out-Null;Copy-Item -LiteralPath "$s\$($p.relative)" -Destination $p.destination -Force;if((Get-FileHash $p.destination).Hash.ToLower() -ne $p.sha256){throw "Install mismatch $($p.relative)"}}
 if((Get-FileHash "$g\PBZ.exe").Hash -ne $exeSha){throw 'Game exe changed'}
 $receipt=[pscustomobject]@{version='0.46';target=$g;verified_files=$plan.Count;backed_up_files=@($plan|Where-Object existed).Count;game_exe_sha256=$exeSha;rollback="$r\rollback.ps1";game_launched=$false;time=(Get-Date -Format o)}
 $receipt|ConvertTo-Json|Set-Content "$r\receipt.json" -Encoding UTF8
 $receipt|ConvertTo-Json
 Get-Content "$g\DLSS5-AMD\default-config.txt" | Select-String '^DLSS5_(MULTI_PASS|TEMPORAL_MODE|VIT_ADAPTIVE|PRE_UPSCALE)='
}catch{
 if($mutated){foreach($p in $plan){if($p.existed){Copy-Item -LiteralPath $p.backup -Destination $p.destination -Force}else{Remove-Item -LiteralPath $p.destination -Force -ErrorAction SilentlyContinue}};'AUTO_RESTORED'}
 throw
}finally{if($held){$held.Dispose();Remove-Item D:\DLSSNR-Lab\gpu.lock};'LOCK_RELEASED'}
