$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-step4-20261031';$mz='D:\DLSSNR-Lab\competitor-timing-20260930\mz'
Get-ChildItem $mz -File -Recurse|Sort-Object FullName|ForEach-Object {@{path=$_.FullName;bytes=$_.Length;mtime=$_.LastWriteTimeUtc.Ticks;sha256=(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json -Depth 4|Set-Content "$r\mochi-tree-after.json"
$a=Get-Content "$r\mochi-tree-before.json" -Raw;$b=Get-Content "$r\mochi-tree-after.json" -Raw;if($a -ne $b){throw 'locked Mochizuki tree changed'};'MOCHI_TREE_UNCHANGED'

Get-ChildItem 'D:\DLSSNR-Lab\history-trial-041a-20261006\assets' -File -Recurse|Sort-Object FullName|ForEach-Object {@{path=$_.FullName;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json -Depth 4|Set-Content "$r\native-assets-after.json"
if((Get-Content "$r\native-assets-before.json" -Raw) -ne (Get-Content "$r\native-assets-after.json" -Raw)){throw 'native assets changed'}
