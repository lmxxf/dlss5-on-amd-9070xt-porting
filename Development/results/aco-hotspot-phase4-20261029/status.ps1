$r='D:\DLSSNR-Lab\aco-hotspot-phase4-20261029'
Get-Process mochi-native,pure,frame -ErrorAction SilentlyContinue|Select-Object ProcessName,Id,CPU,StartTime|ConvertTo-Json
Get-ChildItem "$r\aco-cache" -File -Recurse -ErrorAction SilentlyContinue|Measure-Object Length -Sum|Select-Object Count,Sum|ConvertTo-Json
