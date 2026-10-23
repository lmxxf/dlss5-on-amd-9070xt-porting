$ErrorActionPreference='Stop'
$lock=Test-Path 'D:\DLSSNR-Lab\gpu.lock'
$active=@(Get-Process -ErrorAction SilentlyContinue|Where-Object {$_.ProcessName -match '^(benchmark|candidate|trace|nr_graph|rtc_compile|rtc_compile-current|frame|frame-standard|frame-free1072|limited-frame-standard|limited-frame-free1072|pair-probe|pure|limited-pure)$'}|Select-Object ProcessName,Id)
@{gpu_lock_present=$lock;active_experiments=$active;D_free_bytes=(Get-PSDrive D).Free;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 4
if($lock -or $active.Count -or (Get-PSDrive D).Free -lt 100GB){exit 1}
