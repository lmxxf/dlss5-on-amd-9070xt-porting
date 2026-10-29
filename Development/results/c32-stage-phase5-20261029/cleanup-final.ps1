$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\c32-stage-phase5-20261029'
& 'D:\DLSSNR-Lab\game-check.ps1' 'Shipping SB-Win64 Onimusha re9.exe Magpie SandFall Wuthering Client-Win64 Genshin YuanShen'|Out-Host
if($LASTEXITCODE -ne 1){throw 'not idle'}
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'locked'}
if(Get-Process|Where-Object {$_.ProcessName -match '^(frame.*|[234]-profile|mochi-native|pure.*|rtc_compile.*|occupancy)$'}){throw 'experiment active'}
$removed=@()
foreach($name in @('full','final','final-nmmn','smoke','occupancy34','integration','integration-nmmn','integration-smoke')){
 if(Test-Path "$r\$name"){Get-ChildItem "$r\$name" -File -Recurse|Where-Object {$_.Extension -in '.hsaco','.exe','.f16','.rgb32f','.rgba32f','.ppm'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item $_.FullName -Force}}
}
foreach($name in @('cache','aco-cache','baseline','full-binaries','full-generated')){if(Test-Path "$r\$name"){$removed+=@{path="$r\$name";bytes=0};Remove-Item "$r\$name" -Recurse -Force}}
Get-ChildItem "$r\payload" -File|Where-Object {$_.Extension -in '.rgba32f','.flags' -or $_.Name -match '^[234]-|^occupancy.exe$|^final-.*\.hsaco$'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item $_.FullName -Force}
if(Test-Path "$r\payload\integration-hosts"){Remove-Item "$r\payload\integration-hosts" -Recurse -Force}
Get-ChildItem $r -File|Where-Object {$_.Name -match 'evidence.zip$|full-binaries.zip|native-cache.bin'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item $_.FullName -Force}
@{removed=$removed;gpu_lock_present=(Test-Path 'D:\DLSSNR-Lab\gpu.lock');D_free_bytes=(Get-PSDrive D).Free;formal_payload_retained="$r\payload"}|ConvertTo-Json -Depth 5|Set-Content "$r\final-cleanup.json"
