param([ValidateSet('Install','Restore','Status')][string]$Action='Status',
 [string]$GameDir='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64')
$ErrorActionPreference='Stop';$preview=$PSScriptRoot;$root=Join-Path $GameDir 'DLSS5-AMD';$state=Join-Path $preview 'last-backup.txt'
function Closed {if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Stellar Blade is running. Exit before installing/restoring.'}}
function Hash($p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}
function Restore($backup){
 Closed;$record=Get-Content -LiteralPath "$backup\backup.json" -Raw|ConvertFrom-Json
 if($record.game -ne $GameDir){throw 'Backup belongs to another game directory'}
 foreach($f in $record.files){if($f.existed -and (Hash "$backup\files\$($f.name)") -ne $f.sha256){throw "Backup hash mismatch: $($f.name)"}}
 foreach($f in $record.files){$dest=Join-Path $GameDir $f.name;if($f.existed){Copy-Item -LiteralPath "$backup\files\$($f.name)" -Destination $dest -Force}else{if(Test-Path -LiteralPath $dest){Remove-Item -LiteralPath $dest}}}
 foreach($dir in @($record.createdDirs)|Sort-Object Length -Descending){$path=Join-Path $GameDir $dir;if((Test-Path -LiteralPath $path) -and @(Get-ChildItem -LiteralPath $path -Force).Count -eq 0){Remove-Item -LiteralPath $path}}
 foreach($f in $record.files){if($f.existed -and (Hash (Join-Path $GameDir $f.name)) -ne $f.sha256){throw "Restore hash mismatch: $($f.name)"}}
 Write-Output "RESTORED $backup"
}
if($Action -eq 'Status'){
 Get-Item -LiteralPath "$GameDir\dlss5-amd.addon64"|Select-Object FullName,Length,LastWriteTime
 Get-Content -LiteralPath "$root\native-game-flags.txt"|Select-String 'VIT_ADAPTIVE|VIT_REUSE|NETWORK_HEIGHT'
 if(Test-Path $state){Get-Content $state};exit
}
Closed
if($Action -eq 'Restore'){if(!(Test-Path $state)){throw 'No preview backup recorded'};Restore (Get-Content $state -Raw).Trim();exit}
if(!(Test-Path -LiteralPath "$GameDir\dlss5-amd.addon64") -or !(Test-Path -LiteralPath "$root\native-game-flags.txt")){throw 'Existing OptiScaler DLSS5 install not found'}
$manifest=Get-Content "$preview\manifest.json" -Raw|ConvertFrom-Json
if($manifest.Count -ne 50){throw 'Expected DLL + gain + 48 architecture modules'}
foreach($f in $manifest){if((Hash "$preview\payload\$($f.name)") -ne $f.sha256){throw "Preview hash mismatch: $($f.name)"}}
$backup=Join-Path $preview ('backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'));New-Item -ItemType Directory -Path "$backup\files" -Force|Out-Null
$names=@($manifest|ForEach-Object{$_.name})+@('DLSS5-AMD\native-game-flags.txt');$saved=@();$createdDirs=@{}
foreach($name in $names){$dir=Split-Path (Join-Path $GameDir $name);while($dir -and $dir -ne $GameDir){if(!(Test-Path -LiteralPath $dir)){$createdDirs[$dir.Substring($GameDir.Length+1)]=$true};$dir=Split-Path $dir}}
foreach($name in $names){$source=Join-Path $GameDir $name;$exists=Test-Path -LiteralPath $source;$sha=$null
 if($exists){$dest=Join-Path "$backup\files" $name;New-Item -ItemType Directory -Path (Split-Path $dest) -Force|Out-Null;Copy-Item -LiteralPath $source -Destination $dest;$sha=Hash $dest;if((Hash $source) -ne $sha){throw 'Backup copy mismatch'}}
 $saved += [pscustomobject]@{name=$name;existed=$exists;sha256=$sha}}
[pscustomobject]@{game=$GameDir;files=$saved;createdDirs=@($createdDirs.Keys)}|ConvertTo-Json -Depth 5|Set-Content "$backup\backup.json"
try{
 Closed
 foreach($f in $manifest){$dest=Join-Path $GameDir $f.name;New-Item -ItemType Directory -Path (Split-Path $dest) -Force|Out-Null;Copy-Item -LiteralPath "$preview\payload\$($f.name)" -Destination $dest -Force}
 $flags=@(Get-Content -LiteralPath "$root\native-game-flags.txt"|Where-Object{$_ -notmatch '^DLSS5_(VIT_ADAPTIVE|VIT_REUSE_|VIT_RESIDUAL_|RESIDUAL_|HIP_GRAPH=|HIP_MODULES=|HIP_VIT_BYTE_STREAM=)'})
 $flags+=@('DLSS5_VIT_ADAPTIVE=1','DLSS5_VIT_REUSE_PERIOD=4','DLSS5_VIT_REUSE_GLOBAL=0.22','DLSS5_VIT_REUSE_LOCAL=1.0','DLSS5_VIT_REUSE_IMAGE=0.35','DLSS5_VIT_REUSE_HOTKEY=1',"DLSS5_VIT_REUSE_GAIN=$root\vit-reuse-gain.f32",'DLSS5_HIP_GRAPH=0','DLSS5_HIP_VIT_BYTE_STREAM=0')
 [IO.File]::WriteAllLines("$root\native-game-flags.txt",$flags,(New-Object Text.UTF8Encoding($false)))
 foreach($f in $manifest){if((Hash (Join-Path $GameDir $f.name)) -ne $f.sha256){throw 'Installed payload hash mismatch'}}
 [IO.File]::WriteAllText($state,$backup,(New-Object Text.UTF8Encoding($false)))
 Write-Output "INSTALLED experimental adaptive ViT; F8 switches AE/EXACT; BACKUP=$backup"
}catch{Restore $backup;throw}
