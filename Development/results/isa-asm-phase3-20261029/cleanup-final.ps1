$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\isa-asm-phase3-20261029'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU lock busy'}
if(Get-Process|Where-Object {$_.ProcessName -match '^(frame|pure|nr_graph|rtc_compile.*)$'}){throw 'GPU job present'}
$paths=@();Get-ChildItem $r -File|Where-Object {$_.Extension -eq '.hip' -or $_.Name -match 'evidence.zip$|full-binaries.zip|mochi-cache.bin'}|ForEach-Object {$paths+=$_.FullName;Remove-Item $_.FullName -Force}
foreach($d in @('cache','full-binaries','baseline')){if(Test-Path "$r\$d"){$paths+="$r\$d";Remove-Item "$r\$d" -Recurse -Force}}
@{removed=$paths;payload_retained=$true;lock_absent=!(Test-Path 'D:\DLSSNR-Lab\gpu.lock')}|ConvertTo-Json -Depth 4|Set-Content "$r\final-cleanup.json"
