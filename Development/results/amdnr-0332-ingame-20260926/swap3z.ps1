param([ValidateSet('3z','ours','status')][string]$To='status')
# Stellar Blade: switch between our stack (OptiScaler dxgi.dll + ReShade + dlss5-amd.addon64) and 3zwr1's AMDNR 0.3.3.2
# (their OptiScaler as dxgi.dll + OptiScaler\ + LmxxfNrRuntime.dll/.pak, NrBackend=lmxxf). Our files are renamed *.ours, never deleted.
$ErrorActionPreference='Stop'
$g='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
$src='D:\DLSSNR-Lab\amdnr-0332\pkg'
$zip='C:\Users\lmxxf\Downloads\AMDNR-v0.3.3.2.zip'
$ourDxgi='FBFB6676B829DAD7E020FB830586A16AA0EC6ADD78016DB48EF12E2AE1803231'
$ourFiles=@('dxgi.dll','OptiScaler.ini','ReShade64.dll','dlss5-amd.addon64')
$theirFiles=@('dxgi.dll','OptiScaler.ini','LmxxfNrRuntime.dll','LmxxfNrRuntime.pak')
function Sha($p){(Get-FileHash $p).Hash}
function State{ if(Test-Path "$g\dxgi.dll.ours"){'3z'}elseif((Test-Path "$g\dxgi.dll") -and (Sha "$g\dxgi.dll") -eq $ourDxgi){'ours'}else{'unknown'} }
if($To -eq 'status'){"state=$(State)";exit}
if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'game running'}
if($To -eq '3z'){
  if((State) -ne 'ours'){throw "state is $(State), expected ours"}
  if(!(Test-Path "$src\OptiScaler.dll")){
    New-Item -ItemType Directory -Force $src|Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zip,$src)
  }
  if(Test-Path "$g\OptiScaler"){throw 'an OptiScaler\ folder already exists in the game dir; inspect first'}
  foreach($f in $ourFiles){ if(Test-Path "$g\$f"){ Rename-Item "$g\$f" "$f.ours" } }
  Copy-Item "$src\OptiScaler.dll" "$g\dxgi.dll"
  Copy-Item "$src\LmxxfNrRuntime.dll","$src\LmxxfNrRuntime.pak" $g
  Copy-Item "$src\OptiScaler" "$g\OptiScaler" -Recurse
  $ini=[IO.File]::ReadAllText("$src\OptiScaler.ini")
  $ini=[regex]::Replace($ini,'(?m)^;NrBackend=daniel\s*$','NrBackend=lmxxf')
  if($ini -notmatch '(?m)^NrBackend=lmxxf'){throw 'could not set NrBackend=lmxxf'}
  [IO.File]::WriteAllText("$g\OptiScaler.ini",$ini,(New-Object Text.UTF8Encoding($false)))
  "SWITCHED to 3z AMDNR 0.3.3.2 (NrBackend=lmxxf); ours renamed *.ours"
}else{
  if((State) -ne '3z'){throw "state is $(State), expected 3z"}
  foreach($f in $theirFiles){ if(Test-Path "$g\$f"){ Remove-Item "$g\$f" } }
  Remove-Item "$g\OptiScaler" -Recurse
  foreach($f in $ourFiles){ if(Test-Path "$g\$f.ours"){ Rename-Item "$g\$f.ours" $f } }
  if((Sha "$g\dxgi.dll") -ne $ourDxgi){throw 'restored dxgi hash mismatch'}
  "SWITCHED to ours (3z logs left in place)"
}
