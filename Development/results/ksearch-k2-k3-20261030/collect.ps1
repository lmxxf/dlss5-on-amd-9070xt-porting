param([string]$Case='knife1')
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-k2-k3-20261030';$source=Join-Path $r $Case;$dest=Join-Path $r ($Case+'-evidence')
New-Item -ItemType Directory -Force $dest|Out-Null
Get-ChildItem $source -File -Recurse|Where-Object {$_.Extension -in '.log','.csv','.json','.flags','.txt'}|ForEach-Object {$rel=$_.FullName.Substring($source.Length+1);$target=Join-Path $dest $rel;New-Item -ItemType Directory -Force (Split-Path $target)|Out-Null;Copy-Item $_.FullName $target}
Compress-Archive -Path "$dest\*" -DestinationPath "$r\$Case-evidence.zip" -Force
