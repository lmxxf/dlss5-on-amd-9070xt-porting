$ErrorActionPreference='Stop'
$g='C:\Program Files (x86)\Steam\steamapps\common\PBZ\platform\steam'
$r='D:\DLSSNR-Lab\pbz-install-20261108\compat-sync'
& D:\DLSSNR-Lab\game-check.ps1 'PBZ SB-Win64 Onimusha Magpie'
if($LASTEXITCODE -ne 1){throw 'Game active/check failed'}
$held=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {
 if(Test-Path $r){throw 'Backup already exists'}
 New-Item -ItemType Directory -Force $r|Out-Null
 Copy-Item "$g\DLSS5-AMD\logs" "$r\logs" -Recurse
 Copy-Item "$g\OptiScaler.log","$g\ReShade.log" $r
 $p="$g\DLSS5-AMD\custom-config.txt";$exists=Test-Path $p
 if($exists){Copy-Item $p "$r\custom-config.before.txt";$txt=[IO.File]::ReadAllText($p)}else{$txt=''}
 [pscustomobject]@{custom_existed=$exists;target=$p}|ConvertTo-Json|Set-Content "$r\before.json" -Encoding UTF8
 $txt=[regex]::Replace($txt,'(?m)^DLSS5_PRE_UPSCALE_ASYNC=.*\r?\n?','')
 $txt=$txt.TrimEnd()+"`r`nDLSS5_PRE_UPSCALE_ASYNC=0`r`n"
 [IO.File]::WriteAllText($p,$txt,(New-Object Text.UTF8Encoding($false)))
 Get-Content $p
 Get-FileHash $p
 'SYNC_TRIAL_APPLIED'
}finally{$held.Dispose();Remove-Item D:\DLSSNR-Lab\gpu.lock;'LOCK_RELEASED'}
