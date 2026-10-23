param([string]$Version='0.45',[string]$SourceCommit='',[string]$ConfigDirectory='D:\DLSSNR-Lab\release-045\scripts',[switch]$Resume)
# 0.45 final phase6 frozen payload; pinned complete 0.44 F9-final baselines; no rebuild.
# Default MULTI_PASS=0.5. Prediction defaults 1 but is active only at MULTI_PASS=3; skin defaults 0.
# This script reads no personal game configuration and does not install/publish packages.
$ErrorActionPreference='Stop'
if(!$SourceCommit){throw 'SourceCommit required'}
$out='D:\給網友打包';$lab='D:\DLSSNR-Lab\release-045';$utf8=New-Object Text.UTF8Encoding($false)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$regularSha='dcd468ed836cad86e2cf3c6cc594b13a0ad4f7a67fa3affa49b472b049e59166'
$hostSha='aa3761f2cdb0d9a4653732bd430019ff421b3ce9471fe3697b553dc6d71714a2'
$runtimeSha='d3ea1907de63d9045dbd078221053e3a1202d10c7067b3c90d323437e6fcf722'
$inputShaderSha='b871dfc5d49c9803f43e11679a770f2b9290a1a5d96cb1a7ea18daf02099d20b'
$convertSha='35a1973fc558d7a934cc1190c91f026734405935113568c81e4c7b19c0627b90'
$built="$lab\payload"
$addonPath="$built\dlss5-amd.addon64";$runtimePath="$built\LmxxfNrRuntime.dll"
$moduleRoot="$lab\HIP";$shaderSrc="$lab\source\shaders"
function CheckHash($path,$sha){if((Get-FileHash $path).Hash -ne $sha){throw "HASH mismatch $path"}}
function WriteUtf8($path,$text){[IO.File]::WriteAllText($path,$text,$utf8)}
function HasLine($path,$line){Select-String -Path $path -Pattern ('^'+[regex]::Escape($line)+'\s*$') -Quiet}
function SetIni($text,$section,$key,$value){
 $pattern='(?ms)^\['+[regex]::Escape($section)+'\]\r?\n.*?(?=^\[|\z)'
 $m=[regex]::Match($text,$pattern)
 if(!$m.Success){return $text+"`r`n[$section]`r`n$key=$value`r`n"}
 $part=$m.Value;$kp='(?m)^'+[regex]::Escape($key)+'=[^\r\n]*'
 if([regex]::IsMatch($part,$kp)){$part=[regex]::Replace($part,$kp,"$key=$value")}else{$part=$part.TrimEnd()+"`r`n$key=$value`r`n"}
 return $text.Substring(0,$m.Index)+$part+$text.Substring($m.Index+$m.Length)
}
function VerifyZip($zip,$sums){
 $z=[IO.Compression.ZipFile]::OpenRead($zip)
 try{
  $entries=@{};foreach($e in $z.Entries){if($entries.ContainsKey($e.FullName.Replace('\','/'))){throw 'Duplicate ZIP entry'};$entries[$e.FullName.Replace('\','/')]=$e}
  foreach($line in $sums){$name=$line.Substring(66);$e=$entries[$name];if(!$e){throw "Missing ZIP entry $name"};$s=$e.Open();$h=[Security.Cryptography.SHA256]::Create();try{$got=([BitConverter]::ToString($h.ComputeHash($s))).Replace('-','')}finally{$s.Dispose();$h.Dispose()};if($got -ne $line.Substring(0,64)){throw "ZIP mismatch $name"}}
  $manifestEntry=$entries['SHA256SUMS.txt'];if(!$manifestEntry){throw 'Missing ZIP checksum manifest'}
  $stream=$manifestEntry.Open();$hasher=[Security.Cryptography.SHA256]::Create()
  try{$manifestHash=([BitConverter]::ToString($hasher.ComputeHash($stream))).Replace('-','')}finally{$stream.Dispose();$hasher.Dispose()}
  if($manifestHash -ne (Get-FileHash (Join-Path ([IO.Path]::ChangeExtension($zip,$null)) 'SHA256SUMS.txt')).Hash){throw 'ZIP checksum manifest bytes differ from stage'}
  if($entries.Count -ne $sums.Count+1){throw 'ZIP extra/missing files'}
 }finally{$z.Dispose()}
}
function RunOwned($exe,$arguments,$seconds,$logPrefix){
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$exe;$psi.Arguments=$arguments;$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi
 if(!$process.Start()){throw "Could not start $exe"}
 $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$deadline=[DateTime]::UtcNow.AddSeconds($seconds)
 try{
  while(!$process.HasExited){if([DateTime]::UtcNow -ge $deadline){throw "Owned process timeout ${seconds}s: $exe"};Start-Sleep -Milliseconds 100}
  $process.WaitForExit();$code=$process.ExitCode
 }finally{
  if(!$process.HasExited){taskkill /PID $process.Id /T /F|Out-Null;$process.WaitForExit()}
  $stdout.Result|Set-Content "$logPrefix.stdout.log";$stderr.Result|Set-Content "$logPrefix.stderr.log"
  [pscustomobject]@{pid=$process.Id;exit=$process.ExitCode;limit_seconds=$seconds}|ConvertTo-Json|Set-Content "$logPrefix.exit.json"
 }
 if($code -ne 0){throw "Owned process failed: $exe exit=$code"}
 return $stdout.Result
}
$busy=Get-Process re9,SB-Win64-Shipping,LOP-Win64-Shipping,Magpie,OnimushaWotS,SandFall* -ErrorAction SilentlyContinue
if($busy){throw "Game running ($($busy.Name -join ',')); shader validation and RE9 smoke need the GPU"}
CheckHash $addonPath $regularSha
CheckHash $runtimePath $runtimeSha
CheckHash "$shaderSrc\native_game_rgb_input.hlsl" $inputShaderSha
foreach($n in 'SOURCE-README.txt','HOST-LICENSE.txt','CORE-LICENSE.txt'){if(!(Test-Path "$lab\$n")){throw "missing $n"}}
if(!(Test-Path "$lab\re9-presr-source.tar.gz")){throw 'regenerated re9-presr-source.tar.gz missing'}
# Module payload: the 80 official modules, checked one by one against the release manifest (HIP-SHA256SUMS from the checklist).
$expected=@{};foreach($line in Get-Content "$lab\HIP-SHA256SUMS"){$expected[$line.Substring(66)]=$line.Substring(0,64)}
if($expected.Count -ne 80){throw 'Manifest count'}
$changes=@()
foreach($arch in 'gfx1200','gfx1201'){foreach($f in Get-ChildItem "$moduleRoot\$arch" -Filter *.hsaco){
 $rel="$arch/$($f.Name)";$sha=(Get-FileHash $f.FullName).Hash.ToLower()
 if($expected[$rel] -ne $sha){throw "Module not in manifest or hash differs: $rel"}
 $changes+=[pscustomobject]@{source=$f.FullName;target="DLSS5-AMD/native-game-tiled-assets/HIP/$rel";sha256=$sha}
}}
if($changes.Count -ne 80){throw 'Module count'}
WriteUtf8 "$lab\payload.json" ($changes|ConvertTo-Json)
$variants=@(@{kind='magpie';prefix='Magpie-DLSS5-AMD';flags='hip-magpie-flags.txt';base='0.44'},@{kind='optiscaler';prefix='OptiScaler-DLSS5-AMD';flags='hip-game-flags.txt';base='0.44'},@{kind='re9';prefix='OptiScaler-REFramework-DLSS5-AMD';flags='re9-presr.ini';base='0.44'})
$baselineHashes=@{'Magpie-DLSS5-AMD'='cf812fe7909fbd53b38f54da8abd584698525f81f7ea71cab19ba14bab48e030';'OptiScaler-DLSS5-AMD'='40b4021ad162992ab9a4c587035ed1e821a6bd3cca7d92482514aec66218fee9';'OptiScaler-REFramework-DLSS5-AMD'='0f10b06a73d1b683a7d95e0e3174d6c2cca1ff43c9d215a0929bf934f042927f'}
$results=@()
foreach($v in $variants){
 $name="$($v.prefix)-$Version";$stage=Join-Path $out $name;$zip="$stage.zip";$baseline=Join-Path $out "$($v.prefix)-$($v.base).zip"
 if(!(Test-Path $baseline)){$baseline=Join-Path "$out\history" "$($v.prefix)-$($v.base).zip"}
 if(Test-Path $zip){if(!$Resume){throw "Output exists $zip"};$sums=@(Get-Content "$stage\SHA256SUMS.txt");VerifyZip $zip $sums}
 else{
  if(Test-Path $stage){throw "Incomplete stage exists; inspect before retry: $stage"}
  $baselineSha=((Get-Content "$baseline.sha256" -Raw).Trim() -split '\s+')[0]
  if($baselineSha -ne $baselineHashes[$v.prefix]){throw '0.44 baseline sidecar differs from pinned release receipt'}
  CheckHash $baseline $baselineSha
  [IO.Compression.ZipFile]::ExtractToDirectory($baseline,$stage)
  foreach($line in Get-Content "$stage\SHA256SUMS.txt"){$exp=$line.Substring(0,64);$relative=$line.Substring(66);CheckHash (Join-Path $stage $relative) $exp}
  Write-Output "BASE VERIFIED $name ($baseline)"
  $assets="$stage\DLSS5-AMD\native-game-tiled-assets"
  Get-ChildItem "$assets\HIP" -Recurse -Filter '*.hsaco'|Remove-Item -Force
  foreach($i in $changes){Copy-Item $i.source (Join-Path $stage $i.target) -Force;CheckHash (Join-Path $stage $i.target) $i.sha256}
  foreach($arch in 'gfx1200','gfx1201'){if(@(Get-ChildItem "$assets\HIP\$arch\*.hsaco").Count -ne 40){throw 'Architecture module count'}}
  if(@(Get-ChildItem "$assets\HIP" -Recurse -Filter *.hsaco).Count -ne 80){throw 'Dual architecture count'}
  $moduleSums=@(Get-ChildItem "$assets\HIP" -Recurse -Filter *.hsaco|Sort-Object FullName|ForEach-Object{(Get-FileHash $_.FullName).Hash.ToLower()+'  '+$_.FullName.Substring(("$assets\HIP").Length+1).Replace('\','/')})
  [IO.File]::WriteAllLines("$assets\HIP\SHA256SUMS",$moduleSums,$utf8)
  foreach($arch in 'gfx1200','gfx1201'){$archSums=@(Get-ChildItem "$assets\HIP\$arch" -Filter *.hsaco|Sort-Object Name|ForEach-Object{(Get-FileHash $_.FullName).Hash.ToLower()+'  '+$_.Name});[IO.File]::WriteAllLines("$assets\HIP\$arch\SHA256SUMS",$archSums,$utf8)}
  New-Item -ItemType Directory "$stage\shaders" -Force|Out-Null
  Copy-Item "$shaderSrc\*.hlsl" "$stage\shaders" -Force
  New-Item -ItemType Directory "$stage\sources\configuration" -Force|Out-Null
  foreach($n in 'CONFIGURATION.md','CONFIGURATION.zh-CN.md','hip-game-flags.txt','hip-magpie-flags.txt','hip-re9-flags.txt','custom-config.txt'){Copy-Item "$ConfigDirectory\$n" "$stage\sources\configuration\$n" -Force}
  foreach($n in 'CONFIGURATION.md','CONFIGURATION.zh-CN.md'){$doc=[IO.File]::ReadAllText("$stage\sources\configuration\$n").Replace('../README.md','../../README.en.txt').Replace('../README.zh-CN.md','../../README.txt');WriteUtf8 "$stage\sources\configuration\$n" $doc}
  # Shader sources: every shipped .hlsl/.hlsli is refreshed from the final tree (flat in the package, dx12-network\ in the tree).
  foreach($f in Get-ChildItem $assets -File|Where-Object{$_.Extension -in '.hlsl','.hlsli'}){
   $src=Join-Path $shaderSrc $f.Name;if(!(Test-Path $src)){$src=Join-Path "$shaderSrc\dx12-network" $f.Name}
   if(!(Test-Path $src)){throw "No final source for shipped shader $($f.Name)"}
   Copy-Item $src $f.FullName -Force
  }
  foreach($f in Get-ChildItem $shaderSrc -File|Where-Object{$_.Extension -in '.hlsl','.hlsli'}){Copy-Item $f.FullName (Join-Path $assets $f.Name) -Force}
  # Preserve and verify the unchanged post70 weights from the complete base.
  CheckHash "$assets\post70-history-head.f16" '77c745c4e1d84a6227a2224291d83c37d1e9bd18d45e95add166b1174225ce8f'
  # Small weights remain from the fully verified 0.44 base; validate all 93 below.
  $sm=Get-Content "$assets\small\model-manifest.json" -Raw|ConvertFrom-Json
  if(@($sm.files.PSObject.Properties).Count -ne 93){throw 'Small manifest count'}
  foreach($f in $sm.files.PSObject.Properties){CheckHash (Join-Path "$assets\small" $f.Name) $f.Value.sha256}
  if(@(Get-ChildItem "$assets\small" -File|Where-Object {$_.Name -ne 'model-manifest.json'}).Count -ne 93){throw 'Small weight count'}
  CheckHash "$assets\native_game_rgb_input.hlsl" $inputShaderSha
  if(!(Test-Path "$assets\native_format_convert.hlsl")){throw 'baseline lacks native_format_convert.hlsl'};CheckHash "$assets\native_format_convert.hlsl" $convertSha
  if($v.kind -ne 're9'){
   Copy-Item $addonPath "$stage\dlss5-amd.addon64" -Force;CheckHash "$stage\dlss5-amd.addon64" $regularSha
   # 2026-10-03 config layers: the template ships as default-config.txt (+ custom-config.template.txt); no native-game-flags.txt in the zip
   $flags=& "$PSScriptRoot\stage-config-layers.ps1" -Lab "$stage\DLSS5-AMD" -Template "$ConfigDirectory\$($v.flags)" -CustomTemplate "$ConfigDirectory\custom-config.txt"
   CheckHash $flags (Get-FileHash "$ConfigDirectory\$($v.flags)").Hash
   foreach($n in 'native-game-flags.txt','custom-config.txt'){if(Test-Path "$stage\DLSS5-AMD\$n"){throw "stage still has $n"}}
   foreach($f in 'DLSS5_FIT_LARGE=1','DLSS5_HIP_PDL=1','DLSS5_HIP_WAVE_OWNED=1','DLSS5_HIP_C512_M32=1','DLSS5_HIP_VIT_PROJ_N64=1','DLSS5_NETWORK_HEIGHT=auto','DLSS5_HIP_VIT_STREAM=3','DLSS5_DIRECT_IO=1','DLSS5_FRAME_STATS=0','DLSS5_MAKE_RESIDENT_EVERY=60','DLSS5_HIP_SWIN_RUN=1','DLSS5_FORMAT_FALLBACK=1','DLSS5_HOT_RELOAD=1','DLSS5_NETWORK_1080_ROWS=1152','DLSS5_STYLE=1','DLSS5_SKIP_BLOCKS=','DLSS5_FAST_NUMERIC=1','DLSS5_MULTI_PASS=0.5','DLSS5_MULTI_PASS_PREDICT=1','DLSS5_MULTI_PASS_SKIN_PROTECT=0','DLSS5_MULTI_PASS_SKIP_BLOCKS=','DLSS5_MULTI_PASS_HOTKEY=F9'){if(!(HasLine $flags $f)){throw "flags missing $f"}}
   if($v.kind -eq 'optiscaler'){
    foreach($f in 'DLSS5_PRE_UPSCALE_ASYNC=auto','DLSS5_STRENGTH=auto','DLSS5_VIT_ADAPTIVE=1'){if(!(HasLine $flags $f)){throw "flags missing $f"}}
    # regular package: games that ship their own FSR dll (Cyberpunk 2077) need OptiScaler to leave the FFX inputs to the game dll; a no-op for Stellar Blade
    $ini=[IO.File]::ReadAllText("$stage\OptiScaler.ini");$section=''
    foreach($line in Get-Content "$ConfigDirectory\optiscaler-regular.ini"){if($line -match '^\[(.+)\]'){$section=$matches[1]}elseif($line -match '^([^;#][^=]*)=(.*)$'){$ini=SetIni $ini $section $matches[1] $matches[2]}}
    WriteUtf8 "$stage\OptiScaler.ini" $ini
    if(!(Select-String -Path "$stage\OptiScaler.ini" -Pattern '^EnableFfxInputs=false' -Quiet)){throw 'OptiScaler.ini missing EnableFfxInputs=false'}
   }else{
    if(!(HasLine $flags 'DLSS5_VIT_ADAPTIVE=0')){throw 'magpie flags missing DLSS5_VIT_ADAPTIVE=0'}
   }
   # Regular and Magpie packages both load the add-on through ReShade: ship ReShade.ini with the "press Home" tutorial dismissed (ReShade fills the other defaults).
   $rs="$stage\ReShade.ini";$rsText=if(Test-Path $rs){[IO.File]::ReadAllText($rs)}else{''}
   WriteUtf8 $rs (SetIni $rsText 'OVERLAY' 'TutorialProgress' '4')
   if(!(HasLine $rs 'TutorialProgress=4')){throw 'ReShade.ini missing TutorialProgress=4'}
  }else{
   foreach($n in 're9-present.addon64','dlss5-amd.addon64','ReShade64.dll','ReShade.ini','ReShadePreset.ini','DLSS5-AMD\re9-present-mode.txt'){if(Test-Path "$stage\$n"){Remove-Item "$stage\$n" -Force}}
   CheckHash "$stage\dxgi.dll" $hostSha
   Copy-Item $runtimePath "$stage\LmxxfNrRuntime.dll" -Force;CheckHash "$stage\LmxxfNrRuntime.dll" $runtimeSha
   # The runtime reads the config layers in DLSS5-AMD\ (found by walking up from the assets directory); template = default-config.txt.
   $flags=& "$PSScriptRoot\stage-config-layers.ps1" -Lab "$stage\DLSS5-AMD" -Template "$ConfigDirectory\hip-re9-flags.txt" -CustomTemplate "$ConfigDirectory\custom-config.txt"
   foreach($n in 'native-game-flags.txt','custom-config.txt'){if(Test-Path "$stage\DLSS5-AMD\$n"){throw "re9 stage still has $n"}}
   foreach($f in 'DLSS5_FIT_LARGE=1','DLSS5_HIP_WAVE_OWNED=1','DLSS5_HIP_C512_M32=1','DLSS5_HIP_VIT_PROJ_N64=1','DLSS5_NETWORK_HEIGHT=auto','DLSS5_HIP_VIT_STREAM=3','DLSS5_FRAME_STATS=0','DLSS5_VIT_ADAPTIVE=0','DLSS5_HIP_SWIN_RUN=1','DLSS5_FORMAT_FALLBACK=1','DLSS5_NETWORK_1080_ROWS=1152','DLSS5_STYLE=1','DLSS5_SKIP_BLOCKS=','DLSS5_FAST_NUMERIC=1','DLSS5_MULTI_PASS=0.5','DLSS5_MULTI_PASS_PREDICT=1','DLSS5_MULTI_PASS_SKIN_PROTECT=0','DLSS5_MULTI_PASS_SKIP_BLOCKS='){if(!(HasLine $flags $f)){throw "re9 flags missing $f"}}
   if(Select-String -Path $flags -Pattern '^DLSS5_DIRECT_IO=' -Quiet){throw 're9 flags must not set DLSS5_DIRECT_IO'}
   $ini=[IO.File]::ReadAllText("$stage\OptiScaler.ini");$section=''
   foreach($line in Get-Content "$ConfigDirectory\re9-presr.ini"){
    if($line -match '^\[(.+)\]'){$section=$matches[1]}elseif($line -match '^([^;#][^=]*)=(.*)$'){$ini=SetIni $ini $section $matches[1] $matches[2]}
   }
   WriteUtf8 "$stage\OptiScaler.ini" $ini
   $moduleSums=@(Get-ChildItem "$assets\HIP" -Recurse -Filter *.hsaco|Sort-Object FullName|ForEach-Object{(Get-FileHash $_.FullName).Hash.ToLower()+'  '+$_.FullName.Substring(("$assets\HIP").Length+1).Replace('\','/')})
   [IO.File]::WriteAllLines("$assets\HIP\SHA256SUMS",$moduleSums,$utf8)
   New-Item -ItemType Directory "$stage\sources" -Force|Out-Null
   Copy-Item "$lab\re9-presr-source.tar.gz" "$stage\sources\re9-presr-source.tar.gz" -Force
   Copy-Item "$lab\SOURCE-README.txt" "$stage\sources\README.txt" -Force
   Copy-Item "$lab\HOST-LICENSE.txt" "$stage\OptiScaler-LICENSE.txt" -Force
   Copy-Item "$lab\CORE-LICENSE.txt" "$stage\DLSS5-AMD-LICENSE.txt" -Force
   WriteUtf8 "$stage\UPSTREAM-CREDITS.txt" "Modified OptiScaler host: TheAutomatic https://github.com/TheAutomatic/dlss-5-amd-project/tree/release/1.9.0 (8f71f73bfc836a37936e7cee6701750ad4e8bfec). GPL-3.0 host source is included in sources/re9-presr-source.tar.gz. HIP/core by lmxxf under its included MIT license. PR design credit: TheAutomatic.`n"
   $smokeArgs='"'+"$stage\LmxxfNrRuntime.dll"+'" "'+"$assets\HIP"+'" 32'
   $smokeOutput=RunOwned "$lab\payload\runtime-smoke.exe" $smokeArgs 15 "$lab\$name-runtime-smoke"
   $gpuSmoke='passed staged runtime smoke, hard 15-second limit' 
  }
  New-Item -ItemType Directory "$stage\sources" -Force|Out-Null
  Copy-Item "$lab\re9-presr-source.tar.gz" "$stage\sources\re9-presr-source.tar.gz" -Force
  Copy-Item "$lab\SOURCE-README.txt" "$stage\sources\README.txt" -Force
  foreach($lang in 'zh','en'){$text=[IO.File]::ReadAllText("$ConfigDirectory\package-notes\$($v.kind)-$lang.txt").Replace('@VERSION@',$Version);$text+=if($lang -eq 'zh'){"`n配置说明：sources/configuration/CONFIGURATION.zh-CN.md。默认0.5x（31块 Small）；F9循环0.5→1→2→3；选择3x才用两遍真实网络+预测第三遍，PREDICT=0保留真三遍；肤色保护默认关闭。`n"}else{"`nConfiguration: sources/configuration/CONFIGURATION.md. Default is 0.5x (31-block Small); F9 cycles 0.5 -> 1 -> 2 -> 3; only selecting 3x uses two real passes plus prediction; PREDICT=0 selects real three passes. Skin protection is off by default.`n"};$file=if($lang -eq 'zh'){'README.txt'}else{'README.en.txt'};WriteUtf8 "$stage\$file" $text}
  if(Test-Path "$stage\build-manifest.json"){Remove-Item "$stage\build-manifest.json" -Force}
  WriteUtf8 "$stage\DLSS5-AMD-VERSION.txt" "$name`r`ngit_base_commit=$SourceCommit`r`nsource_dirty=true`r`nsource_archive_sha256=$((Get-FileHash "$lab\re9-presr-source.tar.gz").Hash.ToLower())`r`n"
  $meta=[ordered]@{version=$Version;package=$name;kind=$v.kind;baseline=$baseline;baseline_sha256=$baselineSha;configuration=$v.flags;modules=80;small_weights=93;default_mode='0.5';git_base_commit=$SourceCommit;source_dirty=$true;source_archive_sha256=(Get-FileHash "$lab\re9-presr-source.tar.gz").Hash.ToLower();regular_addon_sha256=$(if($v.kind -ne 're9'){$regularSha}else{$null});host_sha256=$(if($v.kind -eq 're9'){$hostSha}else{$null});runtime_sha256=$(if($v.kind -eq 're9'){$runtimeSha}else{$null});input_shader_sha256=$inputShaderSha;gpu_smoke=$(if($v.kind -eq 're9'){$gpuSmoke}else{$null});fit_large=$true;bit_exact_vs_previous=$false;bit_exact_scope='See final phase6 validation; no blanket equivalence across modes or historical releases';modules_reused=$true;payload_reused=$true;payload_validation='Development/results/gap-phase6-20261023/final-build';note='Frozen phase6 payload; limited QK three shapes; Up pair STOP; Small/default/F9 semantics preserved'}
  WriteUtf8 "$stage\release.json" ($meta|ConvertTo-Json -Depth 5)
  $shaderOutput=RunOwned 'D:\DLSSNR-Lab\compile_fit_shaders.exe' ('"'+$assets+'"') 180 "$lab\$name-shaders"
  $shaderOutput|Set-Content "$lab\$name-shaders.log"
  if($shaderOutput -notmatch 'PASS 44 shader variants compiled with production D3DCompiler'){throw 'Expected 44 successful shader validations'}
  if(Test-Path "$assets\shader-cache"){Remove-Item "$assets\shader-cache" -Recurse -Force}
  if(Test-Path "$stage\DLSS5-AMD\logs"){Get-ChildItem "$stage\DLSS5-AMD\logs" -File|Where-Object{$_.Name -notin '.keep','README.txt'}|Remove-Item -Force}
  if(@(Get-ChildItem $assets -Filter '*.f16').Count -lt 50){throw 'Full model assets missing'}
  $files=@(Get-ChildItem $stage -Recurse -File|Where-Object{$_.FullName -ne "$stage\SHA256SUMS.txt"}|Sort-Object FullName)
  $sums=@($files|ForEach-Object{(Get-FileHash $_.FullName).Hash.ToLower()+'  '+$_.FullName.Substring($stage.Length+1).Replace('\','/')})
  [IO.File]::WriteAllLines("$stage\SHA256SUMS.txt",$sums,$utf8)
  [IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
  VerifyZip $zip $sums
 }
 if((Get-Item $zip).Length -lt 100MB){throw 'Package unexpectedly small; not a complete release'}
 $hash=(Get-FileHash $zip).Hash.ToLower();WriteUtf8 "$zip.sha256" "$hash  $name.zip`n"
 $result=[pscustomobject]@{name=$name;bytes=(Get-Item $zip).Length;sha256=$hash;files=$sums.Count;verified=$true};$results+=$result;$result|ConvertTo-Json -Compress
 WriteUtf8 "$lab\results.json" ($results|ConvertTo-Json)
}
