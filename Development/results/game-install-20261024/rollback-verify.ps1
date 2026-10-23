$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\game-install-20261024'
$err=$null;$tokens=$null;$ast=[Management.Automation.Language.Parser]::ParseFile("$r\rollback.ps1",[ref]$tokens,[ref]$err);if($err.Count){throw 'rollback parse error'}
$m=Get-Content "$r\backup-manifest.json" -Raw -Encoding UTF8|ConvertFrom-Json
$p=Get-Content "$r\install-plan.json" -Raw -Encoding UTF8|ConvertFrom-Json
if($m.Count -ne 1502 -or $p.Count -ne 2){throw 'rollback metadata count'}
'ROLLBACK_PARSE_AND_METADATA_PASS'
