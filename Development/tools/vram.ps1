$c=Get-Counter '\GPU Adapter Memory(*)\Dedicated Usage','\GPU Adapter Memory(*)\Shared Usage' -MaxSamples 1
$c.CounterSamples | ? {$_.CookedValue -gt 0} | % { "{0,-60} {1,8:N0} MB" -f $_.Path.Split('\')[-2..-1] -join ' ', ($_.CookedValue/1MB) }
$p=Get-Counter '\GPU Process Memory(*)\Dedicated Usage' -MaxSamples 1
$p.CounterSamples | ? {$_.CookedValue -gt 100MB} | % { if($_.InstanceName -match 'pid_(\d+)'){ "{0,-24} {1,8:N0} MB" -f (Get-Process -Id $matches[1] -ErrorAction SilentlyContinue).Name, ($_.CookedValue/1MB) } }
