$ErrorActionPreference='Stop';[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$r='D:\DLSSNR-Lab\release-045';$i=Get-Content 'D:\DLSSNR-Lab\game-install-20261024\receipt.json' -Raw -Encoding UTF8|ConvertFrom-Json
foreach($f in $i.installed){$p=Join-Path $i.games[$f.game] $f.relative;if((Get-FileHash -LiteralPath $p).Hash.ToLower() -ne $f.sha256){throw 'installed payload changed after packaging'}}
foreach($c in $i.protected_configs){if((Test-Path -LiteralPath $c.path) -ne $c.existed){throw 'config existence changed'};if($c.existed -and (Get-FileHash -LiteralPath $c.path).Hash.ToLower() -ne $c.sha256){throw 'player config changed after packaging'}}
$lock=Test-Path D:\DLSSNR-Lab\gpu.lock;$busy=@(Get-Process|Where-Object {$_.ProcessName -match '^(runtime-smoke|compile_fit_shaders|rtc_compile.*|frame|pure|benchmark)$'}|Select-Object ProcessName,Id)
if($lock -or $busy.Count -or (Get-PSDrive D).Free -lt 100GB){throw 'final guard failed'}
[ordered]@{version='0.45';installed_items_verified=$i.installed.Count;configuration_records_unchanged=$i.protected_configs.Count;runtime_smoke_exits=@($i.runtime_smokes|ForEach-Object {$_.exit_code});gpu_lock_present=$lock;active_experiments=$busy;D_free_bytes=(Get-PSDrive D).Free;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 5
