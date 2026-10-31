$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'wait for GPU jobs to finish'}
$m=Get-Content "$r\selected-regression.json" -Raw|ConvertFrom-Json
$paths=@{};$dirs=@{}
foreach($j in $m.jobs){
 $paths[$j.exe]=$true
 foreach($v in [regex]::Matches($j.arguments,'"([^"]+)"')){
  $path=$v.Groups[1].Value
  if($path.Contains('{OUT}') -or $path.ToLower().EndsWith('\modules')){continue}
  if(Test-Path -LiteralPath $path -PathType Leaf){$paths[$path]=$true}
  elseif(Test-Path -LiteralPath $path -PathType Container){$dirs[$path]=$true}
  else{throw "Missing actual input $path"}
 }
}
$files=@();foreach($path in $paths.Keys){if(Test-Path -LiteralPath $path){$f=Get-Item -LiteralPath $path;$files+=@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower();bytes=$f.Length;mtime_utc=$f.LastWriteTimeUtc.ToString('o')}}else{$files+=@{path=$path;missing=$true}}}
$files|ConvertTo-Json -Depth 5|Set-Content "$r\actual-input-files.json"
$assets=@();foreach($dir in $dirs.Keys){Get-ChildItem -LiteralPath $dir -File -Recurse|ForEach-Object{$assets+=@{root=$dir;path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower();bytes=$_.Length;mtime_utc=$_.LastWriteTimeUtc.ToString('o')}}}
$assets|ConvertTo-Json -Depth 5|Set-Content "$r\asset-files-before-final.json"
Write-Output "INPUTS $($files.Count) ASSETS $($assets.Count)"
$files|Where-Object{$_.missing}|ConvertTo-Json
