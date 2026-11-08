$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$u=New-Object Text.UTF8Encoding($false)
$stage='D:\給網友打包\OptiScaler-DLSS5-AMD-0.46.1';$zip="$stage.zip";$lab='D:\DLSSNR-Lab\release-0461'
function HashFile($p){(Get-FileHash -LiteralPath $p).Hash.ToLower()}
function W($p,$s){[IO.File]::WriteAllText($p,$s,$u)}
$old=@{};foreach($line in Get-Content 'D:\給網友打包\OptiScaler-DLSS5-AMD-0.46\SHA256SUMS.txt' -Encoding UTF8){$old[$line.Substring(66)]=$line.Substring(0,64)}
$allow=@('dlss5-amd.addon64','README.txt','README.en.txt','DLSS5-AMD-VERSION.txt','release.json')
foreach($rel in $old.Keys){if($rel -notin $allow -and (HashFile (Join-Path $stage $rel)) -ne $old[$rel]){throw "Unexpected changed asset $rel"}}
$meta=Get-Content "$stage\release.json" -Raw -Encoding UTF8|ConvertFrom-Json
$files=@(Get-ChildItem $stage -Recurse -File|Where-Object {$_.FullName -ne "$stage\SHA256SUMS.txt"}|Sort-Object FullName)
$sums=@($files|ForEach-Object {(HashFile $_.FullName)+'  '+$_.FullName.Substring($stage.Length+1).Replace('\','/')})
[IO.File]::WriteAllLines("$stage\SHA256SUMS.txt",$sums,$u)

$z=[IO.Compression.ZipFile]::OpenRead($zip)
try{
 $entries=@{};foreach($e in $z.Entries){if($entries.ContainsKey($e.FullName.Replace('\','/'))){throw 'Duplicate ZIP entry'};$entries[$e.FullName.Replace('\','/')]=$e}
 foreach($f in @(Get-ChildItem $stage -Recurse -File)){
  $rel=$f.FullName.Substring($stage.Length+1).Replace('\','/');$e=$entries[$rel];if(!$e){throw "Missing entry $rel"}
  $stream=$e.Open();$sha=[Security.Cryptography.SHA256]::Create()
  try{$got=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLower()}finally{$stream.Dispose();$sha.Dispose()}
  if($got -ne (HashFile $f.FullName)){throw "ZIP readback mismatch $rel"}
 }
 if($entries.Count -ne $files.Count+1){throw 'ZIP entry count mismatch'}
}finally{$z.Dispose()}
$hash=HashFile $zip;W "$zip.sha256" "$hash  $([IO.Path]::GetFileName($zip))`r`n"
$result=[ordered]@{name=[IO.Path]::GetFileName($zip);bytes=(Get-Item $zip).Length;sha256=$hash;verified_files=$files.Count+1;unchanged_baseline_files=$old.Count-$allow.Count;addon_sha256=$meta.regular_addon_sha256;source_sha256='c5fedfa22cee3ce574b250061f29d2f5bfc6605b8d31020377e8f7eadfb1ee93';user_game_acceptance=$true;gpu_executed=$false}
W "$lab\receipt.json" ($result|ConvertTo-Json -Depth 5)
$result|ConvertTo-Json
'PACKAGE_COMPLETE'
