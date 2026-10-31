$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\ksearch-round4-20261031'
$mz='D:\DLSSNR-Lab\competitor-timing-20260930\mz'
Get-ChildItem $mz -File -Recurse|Sort-Object FullName|ForEach-Object {@{path=$_.FullName;bytes=$_.Length;mtime=$_.LastWriteTimeUtc.Ticks;sha256=(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json -Depth 4|Set-Content "$r\mochi-tree-after.json"
Get-ChildItem 'D:\DLSSNR-Lab\history-trial-041a-20261006\assets' -File -Recurse|Sort-Object FullName|ForEach-Object {@{path=$_.FullName;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json -Depth 4|Set-Content "$r\native-assets-after.json"
Write-Output 'TREES_SNAPSHOTTED'
