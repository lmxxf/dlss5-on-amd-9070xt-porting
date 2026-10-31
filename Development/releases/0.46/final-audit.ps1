$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\release-046';$o='D:\給網友打包'
$packages=Get-Content "$r\results.json" -Raw -Encoding UTF8|ConvertFrom-Json
foreach($x in $packages){
 $s="$o\$($x.name)";if((Get-FileHash "$s.zip").Hash -ne $x.sha256){throw 'zip drift'}
 $cfg="$s\DLSS5-AMD\default-config.txt"
 foreach($l in 'DLSS5_MULTI_PASS=0.5','DLSS5_TEMPORAL_MODE=0','DLSS5_MULTI_PASS_HOTKEY=F9'){if(!(Select-String -LiteralPath $cfg -Pattern ('^'+[regex]::Escape($l)+'\s*$') -Quiet)){throw "default missing $l"}}
 foreach($n in 'custom-config.txt','native-game-flags.txt'){if(Test-Path "$s\DLSS5-AMD\$n"){throw 'personal config'}}
 if((Get-FileHash "$s\sources\re9-presr-source.tar.gz").Hash -ne (Get-FileHash "$r\re9-presr-source.tar.gz").Hash){throw 'source mismatch'}
}
foreach($x in (Get-Content "$r\baseline-045.json" -Raw -Encoding UTF8|ConvertFrom-Json).packages){if((Get-FileHash "$o\history\$($x.name).zip").Hash -ne $x.sha256){throw '045 drift'}}
foreach($x in (Get-Content "$r\history044-before.json" -Raw -Encoding UTF8|ConvertFrom-Json)){if((Get-FileHash (Join-Path "$o\history" ([IO.Path]::GetFileName($x.path)))).Hash -ne $x.sha256){throw '044 drift'}}
$i=Get-Content D:\DLSSNR-Lab\game-install-20261031\receipt.json -Raw -Encoding UTF8|ConvertFrom-Json
if($i.installed.Count -ne 169 -or $i.protected_configs.Count -ne 16){throw 'install count'}
foreach($plan in (Get-Content D:\DLSSNR-Lab\game-install-20261031\install-plan.json -Raw -Encoding UTF8|ConvertFrom-Json)){foreach($x in $plan.items){if((Get-FileHash -LiteralPath $x.destination).Hash -ne $x.sha256){throw 'installed drift'}}}
foreach($c in $i.protected_configs){if((Test-Path -LiteralPath $c.path) -ne $c.existed){throw 'config existence'};if($c.existed -and (Get-FileHash -LiteralPath $c.path).Hash -ne $c.sha256){throw 'config drift'}}
if(Test-Path D:\DLSSNR-Lab\gpu.lock){throw 'lock remains'}
@{version='0.46';status='COMPLETE';packages=$packages;modules_per_package=80;shader_checks_total=132;runtime_smoke_limit_seconds=15;full_zip_file_sha_readback=$true;defaults_verified=$true;no_personal_config_shipped=$true;source_archive_sha256=(Get-FileHash "$r\re9-presr-source.tar.gz").Hash.ToLower();git_base_commit='2f19eebc';source_dirty=$true;history045_preserved=$true;history044_unchanged=$true;installed_files=169;protected_configs=16;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 8|Set-Content "$r\receipt.json" -Encoding UTF8
@{lock_present=$false;disks=@(Get-PSDrive C,D|Select Name,Free);processes=@(Get-Process|Where-Object ProcessName -match '^(runtime-smoke|compile_fit_shaders|frame|pure)$'|Select Id,ProcessName)}|ConvertTo-Json -Depth 5|Set-Content "$r\final-state.json" -Encoding UTF8
'FINAL_AUDIT_PASS'
