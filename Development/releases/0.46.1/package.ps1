$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$u=New-Object Text.UTF8Encoding($false)
$out='D:\給網友打包';$stage="$out\OptiScaler-DLSS5-AMD-0.46.1";$zip="$stage.zip"
$lab='D:\DLSSNR-Lab\release-0461'
function HashFile($p){(Get-FileHash -LiteralPath $p).Hash.ToLower()}
function W($p,$s){[IO.File]::WriteAllText($p,$s,$u)}
if((Get-PSDrive D).Free -lt 100GB){throw 'Disk below100GB'}
if(Test-Path $stage){throw 'Stage exists'}
if(Test-Path $zip){throw 'ZIP exists'}
if((HashFile "$out\OptiScaler-DLSS5-AMD-0.46.zip") -ne 'ead95c097b2e3f399bd39bfe5565e524e8cea2a0ca939a4a751910d94148de94'){throw 'Base ZIP mismatch'}
if((HashFile D:\DLSSNR-Lab\pbz-entry-fix.addon64) -ne 'c762cf36d2a12bceca8750fbcc42fa0e1f8a2aecb2084bbcaf3f259a4e0c7347'){throw 'Addon mismatch'}
if((HashFile D:\DLSSNR-Lab\addon-source-0.46.1.tar.gz) -ne 'c5fedfa22cee3ce574b250061f29d2f5bfc6605b8d31020377e8f7eadfb1ee93'){throw 'Source mismatch'}
New-Item -ItemType Directory -Force $lab|Out-Null
[IO.Compression.ZipFile]::ExtractToDirectory("$out\OptiScaler-DLSS5-AMD-0.46.zip",$stage)
$old=@{};foreach($line in Get-Content "$stage\SHA256SUMS.txt" -Encoding UTF8){$rel=$line.Substring(66);$old[$rel]=$line.Substring(0,64);if((HashFile (Join-Path $stage $rel)) -ne $old[$rel]){throw "Base asset mismatch $rel"}}
'BASE_ALL_FILES_VERIFIED'
Copy-Item D:\DLSSNR-Lab\pbz-entry-fix.addon64 "$stage\dlss5-amd.addon64"
Copy-Item D:\DLSSNR-Lab\addon-source-0.46.1.tar.gz "$stage\sources\addon-source-0.46.1.tar.gz"
Copy-Item D:\DLSSNR-Lab\PBZ-install-notes.txt "$stage\PBZ-安装说明.txt"
New-Item -ItemType Directory "$stage\PBZ-兼容配置"|Out-Null
$ini=[IO.File]::ReadAllText("$stage\OptiScaler.ini")
$ini=[regex]::Replace($ini,'(?m)^Fsr4Update=.*$','Fsr4Update=false')
W "$stage\PBZ-兼容配置\OptiScaler.ini" $ini
W "$stage\PBZ-兼容配置\custom-config.template.txt" "# Merge this line into DLSS5-AMD\custom-config.txt; preserve your other settings.`r`nDLSS5_PRE_UPSCALE_ASYNC=0`r`n"
foreach($n in 'README.txt','README.en.txt'){
 $t=[IO.File]::ReadAllText("$stage\$n");$t=$t.Replace('0.46','0.46.1')
 $t+="`r`n0.46.1: Fix deferred FFX replay using the wrong shim/provider entry. PBZ installation: platform\steam. See PBZ-install notes and optional PBZ compatibility config. User confirmed entering gameplay on RX9070XT; no long-run/FPS claim. Matching addon source: sources/addon-source-0.46.1.tar.gz (git 5f30cc5d).`r`n"
 W "$stage\$n" $t
}
W "$stage\DLSS5-AMD-VERSION.txt" "OptiScaler-DLSS5-AMD-0.46.1`r`ngit_base_commit=5f30cc5d`r`naddon_sha256=c762cf36d2a12bceca8750fbcc42fa0e1f8a2aecb2084bbcaf3f259a4e0c7347`r`naddon_source_sha256=c5fedfa22cee3ce574b250061f29d2f5bfc6605b8d31020377e8f7eadfb1ee93`r`n"
$meta=[ordered]@{version='0.46.1';kind='optiscaler';git_base_commit='5f30cc5d';baseline_sha256='ead95c097b2e3f399bd39bfe5565e524e8cea2a0ca939a4a751910d94148de94';regular_addon_sha256='c762cf36d2a12bceca8750fbcc42fa0e1f8a2aecb2084bbcaf3f259a4e0c7347';addon_source_sha256='c5fedfa22cee3ce574b250061f29d2f5bfc6605b8d31020377e8f7eadfb1ee93';validation='PBZ user confirmed entering game; synchronous pre-upscale and FSR4 update disabled retained during confirmation';modules_reused=$true;default_config_preserved=$true;shader_sources_preserved=$true;gpu_test_run_during_packaging=$false}
W "$stage\release.json" ($meta|ConvertTo-Json -Depth 5)
# Only these existing files may change. Every original model/kernel/shader/config is rechecked.
$allow=@('dlss5-amd.addon64','README.txt','README.en.txt','DLSS5-AMD-VERSION.txt','release.json')
foreach($rel in $old.Keys){if($rel -notin $allow -and (HashFile (Join-Path $stage $rel)) -ne $old[$rel]){throw "Unexpected changed asset $rel"}}
if(Test-Path "$stage\DLSS5-AMD\custom-config.txt"){throw 'Personal config forbidden'}
$files=@(Get-ChildItem $stage -Recurse -File|Where-Object {$_.FullName -ne "$stage\SHA256SUMS.txt"}|Sort-Object FullName)
$sums=@($files|ForEach-Object {(HashFile $_.FullName)+'  '+$_.FullName.Substring($stage.Length+1).Replace('\','/')})
[IO.File]::WriteAllLines("$stage\SHA256SUMS.txt",$sums,$u)
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
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
