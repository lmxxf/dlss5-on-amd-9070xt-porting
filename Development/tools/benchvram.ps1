param([string]$Folder)
Set-Location $Folder
$env:DLSS5_TEST_FRAME_COUNT='40'
Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File $Folder\bench-norebuild.ps1 -Folder $Folder" -WindowStyle Hidden | Out-Null
$best=0
for($i=0;$i -lt 40;$i++){ Start-Sleep -Milliseconds 700; $c=Get-Counter '\GPU Process Memory(*)\Dedicated Usage' -MaxSamples 1 -ErrorAction SilentlyContinue; foreach($s in $c.CounterSamples){ if($s.InstanceName -match 'pid_(\d+)'){ $n=(Get-Process -Id $matches[1] -ErrorAction SilentlyContinue).Name; if($n -like 'native-network70*' -and $s.CookedValue -gt $best){$best=$s.CookedValue} } } }
"bench peak dedicated MB: {0:N0}" -f ($best/1MB)
