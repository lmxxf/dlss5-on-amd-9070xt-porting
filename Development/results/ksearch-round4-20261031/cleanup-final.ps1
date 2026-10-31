$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-round4-20261031'
& 'D:\DLSSNR-Lab\game-check.ps1' 'Shipping SB-Win64 Onimusha re9.exe Magpie SandFall Wuthering Client-Win64 Genshin YuanShen'|Out-Host
if($LASTEXITCODE -ne 1){throw 'not idle'}
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'locked'}
if(Get-Process|Where-Object {$_.ProcessName -match '^(frame.*|[234]-profile|final-profile|runtime-smoke|mochi-native|pure.*|rtc_compile.*|occupancy)$'}){throw 'experiment active'}
$removed=@()
foreach($name in @('final','final-nmmn','final-pure-abba','final-kernel','smoke','probes_sm','r3a_r4a_primitive','r4a','r3a_r2d_4arm','ae_reuse_reg')){
 if(Test-Path "$r\$name"){Get-ChildItem "$r\$name" -File -Recurse|Where-Object {$_.Extension -in '.hsaco','.exe','.f16','.rgb32f','.rgba32f','.ppm','.u32'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item $_.FullName -Force}}
}
foreach($name in @('cache','aco-cache','base','baseline','compile','sm-P0','sm-P1','sm-P2','r3a_r4a_primitive-evidence','r4a-evidence','probes_sm-evidence','arm4-A0_NoR2D','arm4-A1_R2D_C32','arm4-A2_R2D_MH','arm4-A3_R2D_Both','r3a_r2d_4arm-evidence','ae_reuse_reg-evidence','final-evidence','final-nmmn-evidence','final-pure-abba-evidence','final-kernel-evidence','smoke-evidence')){
 if(Test-Path "$r\$name"){$removed+=@{path="$r\$name";bytes=0};Remove-Item "$r\$name" -Recurse -Force}
}
Get-ChildItem "$r\payload" -File|Where-Object {$_.Extension -in '.rgba32f','.flags','.hip' -or $_.Name -match '^(sm-P|r3a_|r4a-|e7b2-)'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item $_.FullName -Force}
Get-ChildItem $r -File|Where-Object {$_.Name -match 'evidence\.zip$|native-cache\.bin'}|ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item $_.FullName -Force}
@{removed=$removed;gpu_lock_present=(Test-Path 'D:\DLSSNR-Lab\gpu.lock');D_free_bytes=(Get-PSDrive D).Free;formal_payload_retained="$r\payload"}|ConvertTo-Json -Depth 5|Set-Content "$r\final-cleanup.json"
