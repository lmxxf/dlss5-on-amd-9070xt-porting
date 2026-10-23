$ErrorActionPreference='Stop'
$lab='D:\DLSSNR-Lab\release-045';$out='D:\給網友打包';$backup="$lab\pre-docfix";$utf8=[Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName System.IO.Compression.FileSystem
if(!(Test-Path 'D:\DLSSNR-Lab\gpu.lock')){throw 'Root-owned lock required'}
if([IO.DriveInfo]::new('D:\').AvailableFreeSpace -lt 100GB){throw 'disk below100GB'}
$identity=Get-Content "$lab\docfix-source-identity.json" -Raw -Encoding UTF8|ConvertFrom-Json
$tar="$lab\re9-presr-source.tar.gz";$newSha=(Get-FileHash $tar).Hash.ToLower();if($newSha -ne $identity.after_sha256){throw 'new tar mismatch'}
function Snapshot($root){$map=@{};foreach($f in Get-ChildItem $root -Recurse -File){$map[$f.FullName.Substring($root.Length+1).Replace('\','/')]=(Get-FileHash $f.FullName).Hash.ToLower()};return $map}
function Verify($zip,$stage){
 $z=[IO.Compression.ZipFile]::OpenRead($zip);$map=Snapshot $stage
 try{$seen=@{};foreach($e in $z.Entries){$n=$e.FullName.Replace('\','/');if($seen.ContainsKey($n)-or !$map.ContainsKey($n)){throw "Unexpected ZIP entry $n"};$seen[$n]=$true;$stream=$e.Open();$h=[Security.Cryptography.SHA256]::Create();try{$sha=([BitConverter]::ToString($h.ComputeHash($stream))).Replace('-','').ToLower()}finally{$stream.Dispose();$h.Dispose()};if($sha -ne $map[$n]){throw "ZIP bytes mismatch $n"}};if($seen.Count -ne $map.Count){throw 'ZIP file count mismatch'}}finally{$z.Dispose()}
 foreach($line in Get-Content "$stage\SHA256SUMS.txt"){$name=$line.Substring(66);if($map[$name] -ne $line.Substring(0,64)){throw "Stage manifest mismatch $name"}}
 if(@(Get-Content "$stage\SHA256SUMS.txt").Count -ne $map.Count-1){throw 'Stage manifest count mismatch'}
}
$allowed=@('sources/re9-presr-source.tar.gz','release.json','DLSS5-AMD-VERSION.txt','SHA256SUMS.txt');$results=@()
New-Item -ItemType Directory $backup -Force|Out-Null
foreach($prefix in 'Magpie-DLSS5-AMD','OptiScaler-DLSS5-AMD','OptiScaler-REFramework-DLSS5-AMD'){
 $name="$prefix-0.45";$stage="$out\$name";$zip="$stage.zip";$temp="$stage.reseal.zip"
 if(Test-Path "$backup\$name.zip"){if((Get-FileHash "$backup\$name.zip").Hash -ne (Get-FileHash $zip).Hash){throw 'Existing backup is not the current original ZIP'}}
 if(Test-Path $temp){throw 'Reseal temporary ZIP already exists'}
 Verify $zip $stage
 $oldZipSha=(Get-FileHash $zip).Hash.ToLower();$side=((Get-Content "$zip.sha256" -Raw).Trim()-split '\s+')[0];if($side-ne$oldZipSha){throw 'Original sidecar mismatch'}
 if((Get-FileHash "$stage\sources\re9-presr-source.tar.gz").Hash -ne $identity.before_sha256){throw 'Original staged source mismatch'}
 if(!(Test-Path "$backup\$name.zip")){Copy-Item $zip "$backup\$name.zip";Copy-Item "$zip.sha256" "$backup\$name.zip.sha256"}
 if((Get-FileHash "$backup\$name.zip").Hash -ne $oldZipSha){throw 'Backup ZIP mismatch'}
 $before=Snapshot $stage
 Copy-Item $tar "$stage\sources\re9-presr-source.tar.gz" -Force
 $meta=Get-Content "$stage\release.json" -Raw -Encoding UTF8|ConvertFrom-Json;$meta.source_archive_sha256=$newSha
 [IO.File]::WriteAllText("$stage\release.json",($meta|ConvertTo-Json -Depth 20),$utf8)
 $version=[IO.File]::ReadAllText("$stage\DLSS5-AMD-VERSION.txt");if(@([regex]::Matches($version,'(?m)^source_archive_sha256=[a-f0-9]+')).Count-ne 1){throw 'Version source SHA field count'}
 $version=[regex]::Replace($version,'(?m)^source_archive_sha256=[a-f0-9]+',"source_archive_sha256=$newSha");[IO.File]::WriteAllText("$stage\DLSS5-AMD-VERSION.txt",$version,$utf8)
 $files=@(Get-ChildItem $stage -Recurse -File|Where-Object{$_.FullName-ne"$stage\SHA256SUMS.txt"}|Sort-Object FullName)
 $sums=@($files|ForEach-Object{(Get-FileHash $_.FullName).Hash.ToLower()+'  '+$_.FullName.Substring($stage.Length+1).Replace('\','/')});[IO.File]::WriteAllLines("$stage\SHA256SUMS.txt",$sums,$utf8)
 $after=Snapshot $stage;if($before.Count-ne$after.Count){throw 'File set changed'}
 $changed=@();foreach($n in $after.Keys){if(!$before.ContainsKey($n)){throw 'Added file'};if($before[$n]-ne$after[$n]){$changed+=$n;if($n-notin$allowed){throw "Non-document payload changed $n"}}};if($changed.Count-ne4){throw 'Expected exactly four changed files'}
 [IO.Compression.ZipFile]::CreateFromDirectory($stage,$temp,[IO.Compression.CompressionLevel]::Optimal,$false)
 Verify $temp $stage
 Move-Item $temp $zip -Force
 $sha=(Get-FileHash $zip).Hash.ToLower();[IO.File]::WriteAllText("$zip.sha256","$sha  $name.zip`n",$utf8)
 $results+=[pscustomobject]@{name=$name;old_zip_sha256=$oldZipSha;sha256=$sha;bytes=(Get-Item $zip).Length;source_archive_sha256=$newSha;changed_files=$changed;files=$after.Count;full_zip_readback=$true;unchanged_payload_files=$after.Count-4}
 $results|ConvertTo-Json -Depth 6|Set-Content "$lab\docfix-results.json"
}
