foreach($k in 'Dedicated Usage','Shared Usage','Total Committed'){ $p=Get-Counter "\GPU Process Memory(*)\$k" -MaxSamples 1 -ErrorAction SilentlyContinue
 $p.CounterSamples | ? {$_.CookedValue -gt 50MB} | % { if($_.InstanceName -match 'pid_(\d+)'){ "{0,-16} {1,-22} {2,8:N0} MB" -f $k,(Get-Process -Id $matches[1] -ErrorAction SilentlyContinue).Name,($_.CookedValue/1MB) } } }
$a=Get-Counter '\GPU Adapter Memory(*)\Dedicated Usage','\GPU Adapter Memory(*)\Shared Usage','\GPU Adapter Memory(*)\Total Committed' -MaxSamples 1
$a.CounterSamples | ? {$_.CookedValue -gt 50MB} | % { "{0,-40} {1,8:N0} MB" -f ($_.Path -replace '.*\\',''), ($_.CookedValue/1MB) }
