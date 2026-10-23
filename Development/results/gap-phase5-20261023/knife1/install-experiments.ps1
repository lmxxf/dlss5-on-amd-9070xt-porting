$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\gap-phase5-20261023'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU busy'}
$old=(Get-FileHash "$r\baseline\modules\c512-m32-mh.hsaco").Hash
$new=(Get-FileHash "$r\knife1\compile-cpu-baseline\c512-m32-mh.hsaco").Hash
if($old -ne $new){throw 'Recompiled baseline differs from production'}
foreach($v in @('pair','perm','combined')){
 $dest="$r\knife1\$v\modules";New-Item -ItemType Directory -Force $dest|Out-Null
 Copy-Item "$r\baseline\modules\*" $dest
 Copy-Item "$r\knife1\compile-cpu-$v\c512-m32-mh.hsaco" $dest
 Get-ChildItem $dest -File | ForEach-Object {[PSCustomObject]@{path=$_.FullName;sha256=(Get-FileHash $_.FullName).Hash.ToLower()}} | ConvertTo-Json | Set-Content "$r\knife1\$v\module-hashes.json"
}
'BASELINE_IDENTICAL_EXPERIMENTS_READY'
