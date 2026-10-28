$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\backfeed-20261028'
& 'D:\DLSSNR-Lab\game-check.ps1' 'Shipping SB-Win64 Onimusha re9.exe Magpie SandFall Wuthering Client-Win64 Genshin YuanShen' | Out-Host
if($LASTEXITCODE -ne 1){throw 'not idle'}
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'lock present'}
if(Get-Process|Where-Object {$_.ProcessName -match '^(frame.*|trace|rtc_compile.*|runtime-smoke|pure.*|benchmark.*)$'}){throw 'experiment active'}
$removed=@()
foreach($scope in @('knife1','knife2','knife3')){
 Get-ChildItem "$r\$scope" -Recurse -File | Where-Object {$_.Extension -in '.f16','.ppm','.rgb32f','.rgba32f','.hsaco','.exe'} | ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item -LiteralPath $_.FullName}
}
Get-ChildItem $r -File | Where-Object {$_.Name -in 'baseline.hip','contract.hip','project.hip','qkv.hip','c64-wave2.hsaco','c64-wave2-fast.hsaco'} | ForEach-Object {$removed+=@{path=$_.FullName;bytes=$_.Length};Remove-Item -LiteralPath $_.FullName}
@{removed=$removed;D_free_bytes=(Get-PSDrive D).Free;gpu_lock_present=(Test-Path 'D:\DLSSNR-Lab\gpu.lock');utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 5|Set-Content "$r\cleanup.json"
'CLEANUP_DONE'
