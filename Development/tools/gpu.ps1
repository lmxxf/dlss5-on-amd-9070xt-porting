$c=Get-Counter '\GPU Engine(*)\Utilization Percentage' -SampleInterval 2 -MaxSamples 1
$rows=$c.CounterSamples | ? {$_.CookedValue -gt 1} | % { if($_.InstanceName -match 'pid_(\d+).*engtype_(\w+)'){ [pscustomobject]@{pid=$matches[1];eng=$matches[2];pct=[math]::Round($_.CookedValue,1)} } }
$rows | group pid,eng | % { $g=$_.Group; [pscustomobject]@{pid=$g[0].pid;name=(Get-Process -Id $g[0].pid -ErrorAction SilentlyContinue).Name;eng=$g[0].eng;pct=($g | measure pct -sum).Sum} } | sort pct -desc | select -first 10 | ft -auto | out-string -width 100
Get-Process | ? {$_.Name -match 'splashtop|srfeature|srserver|SR'} | select Name,Id,@{n='ws_mb';e={[int]($_.WorkingSet64/1MB)}} | ft -auto | out-string -width 80
