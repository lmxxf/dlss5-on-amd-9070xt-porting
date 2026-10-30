$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-round2-20261031'
& 'D:\DLSSNR-Lab\game-check.ps1' 'Shipping SB-Win64 Onimusha re9.exe Magpie SandFall Wuthering Client-Win64 Genshin YuanShen'|Out-Host
if($LASTEXITCODE -ne 1){throw 'not idle'}
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'locked'}
if(Get-Process|Where-Object {$_.ProcessName -match '^(frame.*|[234]-profile|final-profile|runtime-smoke|mochi-native|pure.*|rtc_compile.*|occupancy)$'}){throw 'experiment active'}
$removed=@()
foreach($name in @('r2d-swin-compile','final','final-nmmn','final-pure-abba','final-kernel','smoke','probes','r2a1','r2a2','r2b','r2d_c32','r2d_mh','r2d_both','k5_iso_s1080')){
 if(Test-Path "$r\$name"){Get-ChildItem "$r\$name" -File -Recurse|Where-Object {$_.Extension -in '.hsaco','.exe','.f16','.rgb32f','.rgba32f','.ppm'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item $_.FullName -Force}}
}
foreach($name in @('cache','aco-cache','base','baseline','compile','c32-p0','c32-p1','c32-p2','qkv-p0','qkv-p1','qkv-p2','r2a1-evidence','r2a2-evidence','r2b-evidence','r2d_c32-evidence','r2d_mh-evidence','r2d_both-evidence','k5_iso_s1080-base','k5_iso_s1080-evidence','swin-generated','final-evidence','final-nmmn-evidence','final-pure-abba-evidence','final-kernel-evidence','smoke-evidence')){
 if(Test-Path "$r\$name"){$removed+=@{path="$r\$name";bytes=0};Remove-Item "$r\$name" -Recurse -Force}
}
Get-ChildItem "$r\payload" -File|Where-Object {$_.Extension -in '.rgba32f','.flags' -or $_.Name -match '^(c32-p|r2a|r2b|r2d|k5_iso|r2-profile)'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item $_.FullName -Force}
Get-ChildItem $r -File|Where-Object {$_.Name -match 'evidence\.zip$|native-cache\.bin'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item $_.FullName -Force}
@{removed=$removed;gpu_lock_present=(Test-Path 'D:\DLSSNR-Lab\gpu.lock');D_free_bytes=(Get-PSDrive D).Free;formal_payload_retained="$r\payload"}|ConvertTo-Json -Depth 5|Set-Content "$r\final-cleanup.json"
