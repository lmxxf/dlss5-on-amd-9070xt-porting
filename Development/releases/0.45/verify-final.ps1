$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$r='D:\DLSSNR-Lab\release-045';$out='D:\給網友打包'
$expected=@{};foreach($line in Get-Content "$r\HIP-SHA256SUMS"){$expected[$line.Substring(66)]=$line.Substring(0,64)}
$results=@()
foreach($name in 'Magpie-DLSS5-AMD-0.45','OptiScaler-DLSS5-AMD-0.45','OptiScaler-REFramework-DLSS5-AMD-0.45'){
 $file=Join-Path $out ($name+'.zip');$z=[IO.Compression.ZipFile]::OpenRead($file)
 try{
  $entries=@{};foreach($e in $z.Entries){$key=$e.FullName.Replace('\','/');if($entries.ContainsKey($key)){throw 'duplicate'};$entries[$key]=$e}
  function ReadText($key){$rr=[IO.StreamReader]::new($entries[$key].Open());try{return $rr.ReadToEnd()}finally{$rr.Dispose()}}
  $meta=ReadText 'release.json'|ConvertFrom-Json
  if($meta.version -ne '0.45' -or $meta.git_base_commit -ne 'c24e3a97cdf596d0c997cee7deda91c981d520ce' -or !$meta.source_dirty -or $meta.modules -ne 80){throw 'version/source'}
  $flags=ReadText 'DLSS5-AMD/default-config.txt'
  foreach($line in 'DLSS5_MULTI_PASS=0.5','DLSS5_MULTI_PASS_PREDICT=1','DLSS5_MULTI_PASS_SKIN_PROTECT=0','DLSS5_TEMPORAL_MODE=0','DLSS5_TEMPORAL_ENHANCE_STRENGTH=1'){if($flags -notmatch ('(?m)^'+[regex]::Escape($line)+'
?$')){throw "default $line"}}
  foreach($f in 'DLSS5-AMD/custom-config.txt','DLSS5-AMD/native-game-flags.txt'){if($entries.ContainsKey($f)){throw 'personal config'}}
  $mods=@($entries.Keys|Where-Object{$_ -match '^DLSS5-AMD/native-game-tiled-assets/HIP/(gfx1200|gfx1201)/[^/]+\.hsaco$'})
  if($mods.Count -ne 80){throw "modulecount $($mods.Count)"}
  $sums=ReadText 'SHA256SUMS.txt';$table=@{};foreach($l in ($sums -split "`n")){if($l.Trim()){$table[$l.Trim().Substring(66)]=$l.Substring(0,64)}}
  foreach($rel in $expected.Keys){if($table['DLSS5-AMD/native-game-tiled-assets/HIP/'+$rel] -ne $expected[$rel]){throw "module source $rel"}}
  $wantHost=if($meta.kind -eq 're9'){'d3ea1907de63d9045dbd078221053e3a1202d10c7067b3c90d323437e6fcf722'}else{'dcd468ed836cad86e2cf3c6cc594b13a0ad4f7a67fa3affa49b472b049e59166'}
  $hostKey=if($meta.kind -eq 're9'){'LmxxfNrRuntime.dll'}else{'dlss5-amd.addon64'}
  if($table[$hostKey] -ne $wantHost){throw 'final host hash'}
  if($table['sources/re9-presr-source.tar.gz'] -ne '3bcfa93db081f139b39170ed2d9c23da35b34759fbfb5e5593c709da971c829c' -or $meta.source_archive_sha256 -ne $table['sources/re9-presr-source.tar.gz']){throw 'source snapshot'}
  $template=if($meta.kind -eq 're9'){'hip-re9-flags.txt'}elseif($meta.kind -eq 'magpie'){'hip-magpie-flags.txt'}else{'hip-game-flags.txt'}
  if($table['DLSS5-AMD/default-config.txt'] -ne (Get-FileHash "$r\scripts\$template").Hash.ToLower()){throw 'default template bytes differ'}
  $side=((Get-Content "$file.sha256" -Raw).Trim()-split '\s+')[0];if((Get-FileHash $file).Hash.ToLower() -ne $side){throw 'ZIP sidecar mismatch'}
  $shaderExit=Get-Content "$r\$name-shaders.exit.json" -Raw|ConvertFrom-Json
  if($shaderExit.exit -ne 0 -or !([IO.File]::ReadAllText("$r\$name-shaders.stdout.log").Contains('PASS 44 shader variants compiled with production D3DCompiler'))){throw 'shader 44 gate'}
  if($meta.kind -eq 're9'){$smokeExit=Get-Content "$r\$name-runtime-smoke.exit.json" -Raw|ConvertFrom-Json;if($smokeExit.exit -ne 0 -or $smokeExit.limit_seconds -ne 15){throw 'runtime smoke gate'}}
  $row='DLSS5-AMD/native-game-tiled-assets/post70-history-head.f16';if($entries[$row].Length -ne 64 -or $table[$row] -ne '77c745c4e1d84a6227a2224291d83c37d1e9bd18d45e95add166b1174225ce8f'){throw 'historyrow'}
  $smallPrefix='DLSS5-AMD/native-game-tiled-assets/small/'
  $small=ReadText ($smallPrefix+'model-manifest.json')|ConvertFrom-Json
  if($meta.small_weights -ne 93 -or $meta.default_mode -ne '0.5' -or @($small.files.PSObject.Properties).Count -ne 93){throw 'Small count/default'}
  foreach($prop in $small.files.PSObject.Properties){$key=$smallPrefix+$prop.Name;$entry=$entries[$key];if(!$entry -or $entry.Length -ne $prop.Value.bytes -or $table[$key] -ne $prop.Value.sha256){throw "Small manifest $key"};$stream=$entry.Open();$h=[Security.Cryptography.SHA256]::Create();try{$sha=([BitConverter]::ToString($h.ComputeHash($stream))).Replace('-','').ToLower()}finally{$stream.Dispose();$h.Dispose()};if($sha -ne $prop.Value.sha256){throw "Small ZIP readback $key"}}
  foreach($line in 'DLSS5_HOT_RELOAD=1','DLSS5_MULTI_PASS_HOTKEY=F9'){if($flags -notmatch ('(?m)^'+[regex]::Escape($line)+'\r?$')){throw "hotkey $line"}}
  if(@($entries.Keys|Where-Object{$_ -match 'prefix-main-nt|candidate.hsaco|vtail|score-nohi|host_timing'}).Count){throw 'experimental payload'}
  $payload=@{version=$meta.version;git_base_commit=$meta.git_base_commit;package=$name;bytes=(Get-Item $file).Length;sha256=(Get-FileHash $file).Hash.ToLower();entries=$entries.Count;modules=$mods;module_sums=$expected;flags=$flags;release=$meta;addon_sha256=$table['dlss5-amd.addon64'];runtime_sha256=$table['LmxxfNrRuntime.dll'];entry_names=@($entries.Keys|Sort-Object)}
  $results+=$payload
 }finally{$z.Dispose()}
}
$results|ConvertTo-Json -Depth 10|Set-Content "$r\independent-package-metadata.json" -Encoding UTF8
'ALL3_CONTENT_DEFAULTS_MODULE_SOURCE80_PASS'

$old=@{'Magpie-DLSS5-AMD'='cf812fe7909fbd53b38f54da8abd584698525f81f7ea71cab19ba14bab48e030';'OptiScaler-DLSS5-AMD'='40b4021ad162992ab9a4c587035ed1e821a6bd3cca7d92482514aec66218fee9';'OptiScaler-REFramework-DLSS5-AMD'='0f10b06a73d1b683a7d95e0e3174d6c2cca1ff43c9d215a0929bf934f042927f'}
foreach($prefix in $old.Keys){if((Get-FileHash "$out\history\$prefix-0.44.zip").Hash.ToLower() -ne $old[$prefix]){throw 'old044 modified'}}
'OLD044_PRESERVED_ALL045_GATES_PASS'
