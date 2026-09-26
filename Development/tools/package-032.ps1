param([string]$Version='0.32',[string]$SourceCommit='',[string]$ConfigDirectory='D:\DLSSNR-Lab\release-032\scripts',[switch]$Resume)
# 0.32: three full packages from the 0.31 baselines. Changes: regular add-on 5950fe20 (shared-buffer pool: resolution/preset
# switches no longer leak ~70-90 MB of VRAM each; PR #9 shared bridge header), c32-wave1 (vector input reads, bit-exact,
# about -0.9%) x two architectures, RE9 runtime 2aedb521 (reads DLSS5_HIP_* / SKIP_BLOCKS / FIT_LARGE / NETWORK_HEIGHT from
# DLSS5-AMD\native-game-flags.txt, 0.31 kernel groups on by default, shared-buffer pool, PR #9, ABI-2 host compatible),
# RE9 flags template. Every binary equals the copy installed and played (Stellar Blade / RE9) on the test machine.
$ErrorActionPreference='Stop'
if(!$SourceCommit){throw 'SourceCommit required'}
$out='D:\給網友打包';$lab='D:\DLSSNR-Lab\release-032';$utf8=New-Object Text.UTF8Encoding($false)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$regularSha='5950fe20d68366c3104f68da58eaad6756cc3c839e914d403ecdd5088894236b'
$hostSha='0ef102295a759b51c0c7cba6b8eedb455e9759f5a2b7f9b96309e030c6cd0035'
$runtimeSha='2aedb521579732c4dd2e31c4cf46ff7cad0b21d5c710d24fd9f75641d090f9a0'
$hostPath='D:\DLSSNR-Lab\re9-presr\resize-candidate\bin\OptiScaler.dll';$runtimePath='D:\DLSSNR-Lab\re9-runtime-flags-20260926\deploy\payload\LmxxfNrRuntime.dll'
$stellar='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64\DLSS5-AMD\native-game-tiled-assets\HIP'
function CheckHash($path,$sha){if((Get-FileHash $path).Hash -ne $sha){throw "HASH mismatch $path"}}
function WriteUtf8($path,$text){[IO.File]::WriteAllText($path,$text,$utf8)}
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
  $entries=@{};foreach($e in $z.Entries){if($entries.ContainsKey($e.FullName)){throw 'Duplicate ZIP entry'};$entries[$e.FullName.Replace('\','/')]=$e}
  foreach($line in $sums){$name=$line.Substring(66);$e=$entries[$name];if(!$e){throw "Missing ZIP entry $name"};$s=$e.Open();$h=[Security.Cryptography.SHA256]::Create();try{$got=([BitConverter]::ToString($h.ComputeHash($s))).Replace('-','')}finally{$s.Dispose();$h.Dispose()};if($got -ne $line.Substring(0,64)){throw "ZIP mismatch $name"}}
  if($entries.Count -ne $sums.Count+1){throw 'ZIP extra/missing files'}
 }finally{$z.Dispose()}
}
CheckHash "$lab\dlss5-amd.addon64" $regularSha
CheckHash $hostPath $hostSha
CheckHash $runtimePath $runtimeSha
# Module payload: the vector-input c32-wave1 (both architectures) from the Stellar Blade install; identical copies were
# installed in RE9. The other 28 modules per architecture stay exactly as in 0.31.
$changes=@()
foreach($arch in 'gfx1200','gfx1201'){
 $src="$stellar\$arch\c32-wave1.hsaco";$sha=(Get-FileHash $src).Hash.ToLower()
 $changes+=[pscustomobject]@{source=$src;target="DLSS5-AMD/native-game-tiled-assets/HIP/$arch/c32-wave1.hsaco";sha256=$sha}
}
WriteUtf8 "$lab\payload.json" ($changes|ConvertTo-Json)
$variants=@(@{kind='magpie';prefix='Magpie-DLSS5-AMD';flags='hip-magpie-flags.txt';base='0.31'},@{kind='optiscaler';prefix='OptiScaler-DLSS5-AMD';flags='hip-game-flags.txt';base='0.31'},@{kind='re9';prefix='OptiScaler-REFramework-DLSS5-AMD';flags='re9-presr.ini';base='0.31'})
$results=@()
foreach($v in $variants){
 $name="$($v.prefix)-$Version";$stage=Join-Path $out $name;$zip="$stage.zip";$baseline=Join-Path $out "$($v.prefix)-$($v.base).zip"
 if(!(Test-Path $baseline)){$baseline=Join-Path "$out\history" "$($v.prefix)-$($v.base).zip"}
 if(Test-Path $zip){if(!$Resume){throw "Output exists $zip"};$sums=@(Get-Content "$stage\SHA256SUMS.txt");VerifyZip $zip $sums}
 else{
  if(Test-Path $stage){throw "Incomplete stage exists; inspect before retry: $stage"}
  $baselineSha=((Get-Content "$baseline.sha256" -Raw).Trim() -split '\s+')[0]
  CheckHash $baseline $baselineSha
  [IO.Compression.ZipFile]::ExtractToDirectory($baseline,$stage)
  foreach($line in Get-Content "$stage\SHA256SUMS.txt"){$expected=$line.Substring(0,64);$relative=$line.Substring(66);CheckHash (Join-Path $stage $relative) $expected}
  Write-Output "BASE VERIFIED $name ($baseline)"
  $assets="$stage\DLSS5-AMD\native-game-tiled-assets"
  foreach($i in $changes){Copy-Item $i.source (Join-Path $stage $i.target) -Force;CheckHash (Join-Path $stage $i.target) $i.sha256}
  foreach($n in 'native_codec_encode.hlsl','native_codec_decode.hlsl'){Copy-Item "$lab\$n" "$assets\$n" -Force;CheckHash "$assets\$n" (Get-FileHash "$lab\$n").Hash}
  foreach($arch in 'gfx1200','gfx1201'){if(@(Get-ChildItem "$assets\HIP\$arch\*.hsaco").Count -ne 29){throw 'Architecture module count'}}
  if(@(Get-ChildItem "$assets\HIP" -Recurse -Filter *.hsaco).Count -ne 58){throw 'Dual architecture count'}
  if($v.kind -ne 're9'){
   Copy-Item "$lab\dlss5-amd.addon64" "$stage\dlss5-amd.addon64" -Force;CheckHash "$stage\dlss5-amd.addon64" $regularSha
   Copy-Item "$ConfigDirectory\$($v.flags)" "$stage\DLSS5-AMD\native-game-flags.txt" -Force
   CheckHash "$stage\DLSS5-AMD\native-game-flags.txt" (Get-FileHash "$ConfigDirectory\$($v.flags)").Hash
   if(!(Select-String -Path "$stage\DLSS5-AMD\native-game-flags.txt" -Pattern '^DLSS5_FIT_LARGE=1' -Quiet)){throw 'flags missing DLSS5_FIT_LARGE=1'}
   if(!(Select-String -Path "$stage\DLSS5-AMD\native-game-flags.txt" -Pattern '^DLSS5_HIP_PDL=1' -Quiet)){throw 'flags missing DLSS5_HIP_PDL=1'}
   foreach($f in 'DLSS5_HIP_WAVE_OWNED=1','DLSS5_HIP_C512_M32=1','DLSS5_HIP_VIT_PROJ_N64=1','DLSS5_NETWORK_HEIGHT=auto'){if(!(Select-String -Path "$stage\DLSS5-AMD\native-game-flags.txt" -Pattern ('^'+$f) -Quiet)){throw "flags missing $f"}}
   if($v.kind -eq 'optiscaler'){
    if(!(Select-String -Path "$stage\DLSS5-AMD\native-game-flags.txt" -Pattern '^DLSS5_PRE_UPSCALE_ASYNC=auto' -Quiet)){throw 'flags missing ASYNC=auto'}
    if(!(Select-String -Path "$stage\DLSS5-AMD\native-game-flags.txt" -Pattern '^DLSS5_STRENGTH=auto' -Quiet)){throw 'flags missing STRENGTH=auto'}
    # regular package: games that ship their own FSR dll (Cyberpunk 2077) need OptiScaler to leave the FFX inputs to the game dll; a no-op for Stellar Blade
    $ini=[IO.File]::ReadAllText("$stage\OptiScaler.ini");$section=''
    foreach($line in Get-Content "$ConfigDirectory\optiscaler-regular.ini"){if($line -match '^\[(.+)\]'){$section=$matches[1]}elseif($line -match '^([^;#][^=]*)=(.*)$'){$ini=SetIni $ini $section $matches[1] $matches[2]}}
    WriteUtf8 "$stage\OptiScaler.ini" $ini
    if(!(Select-String -Path "$stage\OptiScaler.ini" -Pattern '^EnableFfxInputs=false' -Quiet)){throw 'OptiScaler.ini missing EnableFfxInputs=false'}
   }
  }else{
   foreach($n in 're9-present.addon64','dlss5-amd.addon64','ReShade64.dll','ReShade.ini','ReShadePreset.ini','DLSS5-AMD\re9-present-mode.txt'){if(Test-Path "$stage\$n"){Remove-Item "$stage\$n" -Force}}
   Copy-Item $hostPath "$stage\dxgi.dll" -Force
   Copy-Item $runtimePath "$stage\LmxxfNrRuntime.dll" -Force
   CheckHash "$stage\dxgi.dll" $hostSha;CheckHash "$stage\LmxxfNrRuntime.dll" $runtimeSha
   # The runtime reads DLSS5_FIT_LARGE from DLSS5-AMD\native-game-flags.txt (found by walking up from the assets directory).
   Copy-Item "$ConfigDirectory\hip-re9-flags.txt" "$stage\DLSS5-AMD\native-game-flags.txt" -Force
   if(!(Select-String -Path "$stage\DLSS5-AMD\native-game-flags.txt" -Pattern '^DLSS5_FIT_LARGE=1' -Quiet)){throw 're9 flags missing DLSS5_FIT_LARGE=1'}
   foreach($f in 'DLSS5_HIP_WAVE_OWNED=1','DLSS5_HIP_C512_M32=1','DLSS5_HIP_VIT_PROJ_N64=1','DLSS5_NETWORK_HEIGHT=auto'){if(!(Select-String -Path "$stage\DLSS5-AMD\native-game-flags.txt" -Pattern ('^'+$f) -Quiet)){throw "re9 flags missing $f"}}
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
   $busy=Get-Process re9,SB-Win64-Shipping,LOP-Win64-Shipping,Magpie,OnimushaWotS -ErrorAction SilentlyContinue
   if($busy){Write-Output 'GPU smoke not repeated: game active; runtime already verified in RE9 by the user';$gpuSmoke='not repeated: game active; user-tested runtime'}
   else{& 'D:\DLSSNR-Lab\re9-presr\runtime-smoke.exe' "$stage\LmxxfNrRuntime.dll" "$assets\HIP" 32;if($LASTEXITCODE){throw 'Staged RE9 runtime validation failed'};$gpuSmoke='passed in staged package'}
  }
  foreach($lang in 'zh','en'){$text=[IO.File]::ReadAllText("$ConfigDirectory\package-notes\$($v.kind)-$lang.txt").Replace('@VERSION@',$Version);$file=if($lang -eq 'zh'){'README.txt'}else{'README.en.txt'};WriteUtf8 "$stage\$file" $text}
  if(Test-Path "$stage\build-manifest.json"){Remove-Item "$stage\build-manifest.json" -Force}
  WriteUtf8 "$stage\DLSS5-AMD-VERSION.txt" "$name`r`nsource_commit=$SourceCommit`r`n"
  $meta=[ordered]@{version=$Version;package=$name;kind=$v.kind;baseline=$baseline;baseline_sha256=$baselineSha;configuration=$v.flags;modules=58;source_commit=$SourceCommit;regular_addon_sha256=$(if($v.kind -ne 're9'){$regularSha}else{$null});host_sha256=$(if($v.kind -eq 're9'){$hostSha}else{$null});runtime_sha256=$(if($v.kind -eq 're9'){$runtimeSha}else{$null});gpu_smoke=$(if($v.kind -eq 're9'){$gpuSmoke}else{$null});fit_large=$true}
  WriteUtf8 "$stage\release.json" ($meta|ConvertTo-Json -Depth 5)
  & 'D:\DLSSNR-Lab\compile_fit_shaders.exe' $assets
  if($LASTEXITCODE){throw 'Staged shader compile validation failed'}
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
