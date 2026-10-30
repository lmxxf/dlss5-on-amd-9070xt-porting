$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-round3-20261031'
& 'D:\DLSSNR-Lab\game-check.ps1' 'Shipping SB-Win64 Onimusha re9.exe Magpie SandFall Wuthering Client-Win64 Genshin YuanShen'|Out-Host
if($LASTEXITCODE -ne 1){throw 'not idle'}
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'locked'}
if(Get-Process|Where-Object {$_.ProcessName -match '^(frame.*|[234]-profile|final-profile|runtime-smoke|mochi-native|pure.*|rtc_compile.*|occupancy)$'}){throw 'experiment active'}
$removed=@()
foreach($name in @('final','final-nmmn','final-pure-abba','final-kernel','smoke','probes_sm','r2d_primitive','r3a','r3b','r3c','s1080_4arm','multi_reg')){
 if(Test-Path "$r\$name"){Get-ChildItem "$r\$name" -File -Recurse|Where-Object {$_.Extension -in '.hsaco','.exe','.f16','.rgb32f','.rgba32f','.ppm'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item $_.FullName -Force}}
}
foreach($name in @('cache','aco-cache','base','baseline','compile','sm-P0','sm-P1','sm-P2','r2d_primitive-evidence','r3a-evidence','r3b-evidence','r3c-evidence','probes_sm-evidence','s1080-Base_K5','s1080-K5_R2D_C32','s1080-K5_R2D_MH','s1080-K5_R2D_Both','r3a_final','s1080_4arm-evidence','multi_reg-evidence','final-evidence','final-nmmn-evidence','final-pure-abba-evidence','final-kernel-evidence','smoke-evidence')){
 if(Test-Path "$r\$name"){$removed+=@{path="$r\$name";bytes=0};Remove-Item "$r\$name" -Recurse -Force}
}
Get-ChildItem "$r\payload" -File|Where-Object {$_.Extension -in '.rgba32f','.flags','.hip' -or $_.Name -match '^(sm-P|r2d|r3a|r3b|r3c|e7b2|r3-profile|frame-history)'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item $_.FullName -Force}
Get-ChildItem $r -File|Where-Object {$_.Name -match 'evidence\.zip$|native-cache\.bin'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item $_.FullName -Force}
@{removed=$removed;gpu_lock_present=(Test-Path 'D:\DLSSNR-Lab\gpu.lock');D_free_bytes=(Get-PSDrive D).Free;formal_payload_retained="$r\payload"}|ConvertTo-Json -Depth 5|Set-Content "$r\final-cleanup.json"
