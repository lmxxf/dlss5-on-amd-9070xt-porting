param([string]$RestoreBackup='')
# Shared-buffer pool add-on (HIP driver never returns imported+mapped D3D12 buffers; results/vram-leak-20260926).
# Replaces only dlss5-amd.addon64 (+ _storage_ copy) on top of 0.31 (106ff3d0). Modules/flags unchanged. PREPARED, NOT EXECUTED.
$ErrorActionPreference='Stop'
$g='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$new='5950FE20D68366C3104F68DA58EAAD6756CC3C839E914D403ECDD5088894236B'
$prev='106FF3D065E2848C3D59A4445293ADD79368B0DB6C4EF3EB16D89BA62CE6BE6F'
$targets=@('dlss5-amd.addon64')+@(if(Test-Path "$g\_storage_\dlss5-amd.addon64"){'_storage_\dlss5-amd.addon64'})
if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'game running'}
if($RestoreBackup){foreach($t in $targets){Copy-Item "$RestoreBackup\$($t -replace '\\','_')" "$g\$t" -Force};"RESTORED $RestoreBackup";exit}
if((Get-FileHash "$root\dlss5-amd.addon64").Hash -ne $new){throw 'candidate hash mismatch'}
foreach($t in $targets){$h=(Get-FileHash "$g\$t").Hash;if($h -ne $prev -and $h -ne $new){throw "installed $t is not 0.31 add-on ($h)"}}
$b="$root\backups\$(Get-Date -Format yyyyMMdd-HHmmss)";New-Item -ItemType Directory -Force $b|Out-Null
foreach($t in $targets){Copy-Item "$g\$t" "$b\$($t -replace '\\','_')"}
foreach($t in $targets){Copy-Item "$root\dlss5-amd.addon64" "$g\$t" -Force;if((Get-FileHash "$g\$t").Hash -ne $new){throw "verify failed $t"}}
"INSTALLED $($targets.Count) add-on file(s) $($new.Substring(0,8)); BACKUP=$b"
