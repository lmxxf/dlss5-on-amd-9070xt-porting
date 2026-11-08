$ErrorActionPreference='Stop'
$r=$PSScriptRoot
& D:\DLSSNR-Lab\game-check.ps1 'PBZ SB-Win64 Onimusha Magpie'
if($LASTEXITCODE -ne 1){throw 'Game active/check failed'}
$held=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {foreach($p in (Get-Content "$r\plan.json" -Raw -Encoding UTF8|ConvertFrom-Json)){if($p.existed){Copy-Item -LiteralPath $p.backup -Destination $p.destination -Force;if((Get-FileHash $p.destination).Hash.ToLower() -ne $p.oldsha){throw 'Restore mismatch'}}else{Remove-Item -LiteralPath $p.destination -Force -ErrorAction SilentlyContinue}};'ROLLBACK_COMPLETE'}finally{$held.Dispose();Remove-Item D:\DLSSNR-Lab\gpu.lock}
