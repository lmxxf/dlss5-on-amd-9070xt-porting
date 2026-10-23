$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\game-install-20261024\replace-probe';New-Item -ItemType Directory -Force $r|Out-Null
[IO.File]::WriteAllText("$r\old",'old');[IO.File]::WriteAllText("$r\new",'new');[IO.File]::Replace("$r\new","$r\old","$r\previous")
if([IO.File]::ReadAllText("$r\old") -ne 'new' -or [IO.File]::ReadAllText("$r\previous") -ne 'old'){throw 'replace probe failed'}
'REPLACE_PROBE_PASS'
