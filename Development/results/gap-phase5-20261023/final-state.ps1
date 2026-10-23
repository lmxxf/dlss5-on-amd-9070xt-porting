$ErrorActionPreference='Stop'
$lock=Test-Path 'D:\DLSSNR-Lab\gpu.lock'
$active=@(Get-Process -ErrorAction SilentlyContinue|Where-Object {$_.ProcessName -match '^(benchmark|candidate|trace|nr_graph|rtc_compile|rtc_compile-current|frame|final-frame-standard|final-frame-free1072|helper-probe|pure)$'}|Select-Object ProcessName,Id)
@{gpu_lock_present=$lock;active_experiments=$active;D_free_bytes=(Get-PSDrive D).Free;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 4
if($lock -or $active.Count){exit 1}
