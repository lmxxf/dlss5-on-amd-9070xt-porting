$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\release-045';$out='D:\給網友打包'
if(Test-Path D:\DLSSNR-Lab\gpu.lock){throw 'owner still active'}
Copy-Item "$r\results.json" "$r\pre-docfix-results.json" -Force
$rows=@();foreach($x in (Get-Content "$r\docfix-results.json" -Raw -Encoding UTF8|ConvertFrom-Json)){
 if(!$x.full_zip_readback -or $x.changed_files.Count -ne 4){throw 'docfix incomplete'}
 $zip="$out\$($x.name).zip";if((Get-FileHash $zip).Hash.ToLower() -ne $x.sha256){throw 'final zip hash'}
 $rows+=[pscustomobject]@{name=$x.name;bytes=$x.bytes;sha256=$x.sha256;files=([int]$x.files-1);zip_entries=$x.files;verified=$true;source_archive_sha256=$x.source_archive_sha256}
 Copy-Item "$zip.sha256" "$r\$($x.name).zip.sha256" -Force
}
if($rows.Count -ne 3){throw 'need three complete final packages'}
$rows|ConvertTo-Json -Depth 6|Set-Content -Encoding UTF8 "$r\results.json"
& "$r\verify-final.ps1"
if(!$?){throw 'final verifier failed'}
'PUBLISH_RECEIPTS_PASS'
