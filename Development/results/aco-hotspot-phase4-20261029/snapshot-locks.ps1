$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\aco-hotspot-phase4-20261029';$mz='D:\DLSSNR-Lab\competitor-timing-20260930\mz'
Get-ChildItem $mz -File -Recurse|Sort-Object FullName|ForEach-Object {@{path=$_.FullName;bytes=$_.Length;mtime=$_.LastWriteTimeUtc.Ticks;sha256=(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json -Depth 4|Set-Content "$r\mochi-tree-before.json"
Copy-Item "$mz\pc.bin" "$r\mochi-cache.bin"

Get-ChildItem 'D:\DLSSNR-Lab\history-trial-041a-20261006\assets' -File -Recurse|Sort-Object FullName|ForEach-Object {@{path=$_.FullName;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json -Depth 4|Set-Content "$r\native-assets-before.json"
