$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\aco-hotspot-phase4-20261029'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU lock busy'}
if(Get-Process|Where-Object {$_.ProcessName -match '^(mochi-native|pure.*|frame.*|rtc_compile.*)$'}){throw 'GPU benchmark process present'}
$removed=@()
foreach($d in @('baseline','cache','aco-cache')){if(Test-Path "$r\$d"){$removed+="$r\$d";Remove-Item "$r\$d" -Recurse -Force}}
Get-ChildItem $r -File|Where-Object {$_.Name -like '*evidence.zip'}|ForEach-Object {$removed+=$_.FullName;Remove-Item $_.FullName -Force}
Get-ChildItem "$r\payload" -File|Where-Object {($_.Name -like 'pure-profile*.exe' -and $_.Name -ne 'pure-profile-device-final.exe') -or $_.Name -like '3?-c64-wave2-fast.hsaco'}|ForEach-Object {$removed+=$_.FullName;Remove-Item $_.FullName -Force}
Get-ChildItem "$r\payload" -File -Recurse|Where-Object {$_.Name -like '*.pre.comp'}|ForEach-Object {$removed+=$_.FullName;Remove-Item $_.FullName -Force}
@{removed=$removed;payload_retained=$true;payload_bytes=(Get-ChildItem "$r\payload" -File -Recurse|Measure-Object Length -Sum).Sum;lock_absent=!(Test-Path 'D:\DLSSNR-Lab\gpu.lock');free_D_GB=(Get-PSDrive D).Free/1GB;benchmark_processes_absent=$true}|ConvertTo-Json -Depth 4|Set-Content "$r\final-cleanup.json"
