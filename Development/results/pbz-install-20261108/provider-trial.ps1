$ErrorActionPreference='Stop'
$g='C:\Program Files (x86)\Steam\steamapps\common\PBZ\platform\steam'
$r='D:\DLSSNR-Lab\pbz-install-20261108\provider-trial'
& D:\DLSSNR-Lab\game-check.ps1 'PBZ SB-Win64 Onimusha Magpie'
if($LASTEXITCODE -ne 1){throw 'Game active/check failed'}
$held=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {
 if(Test-Path $r){throw 'Backup exists'}
 New-Item -ItemType Directory -Force $r|Out-Null
 Copy-Item "$g\DLSS5-AMD\logs" "$r\logs" -Recurse
 Copy-Item "$g\OptiScaler.log","$g\ReShade.log","$g\OptiScaler.ini" $r
 $p="$g\OptiScaler.ini";$txt=[IO.File]::ReadAllText($p)
 if(@([regex]::Matches($txt,'(?m)^Fsr4Update=.*$')).Count -ne 1){throw 'Ambiguous Fsr4Update key'}
 $txt=[regex]::Replace($txt,'(?m)^Fsr4Update=.*$','Fsr4Update=false')
 [IO.File]::WriteAllText($p,$txt,(New-Object Text.UTF8Encoding($false)))
 if([IO.File]::ReadAllText($p) -ne $txt){throw 'Readback failed'}
 Get-Content $p | Select-String '^Fsr4Update=|^Dx12Upscaler=|^EnableFfxInputs='
 Get-FileHash $p
 'PROVIDER_TRIAL_APPLIED'
}finally{$held.Dispose();Remove-Item D:\DLSSNR-Lab\gpu.lock;'LOCK_RELEASED'}
