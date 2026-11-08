$ErrorActionPreference='Stop'
$g='C:\Program Files (x86)\Steam\steamapps\common\PBZ\platform\steam'
$r='D:\DLSSNR-Lab\pbz-install-20261108\entry-fix'
& D:\DLSSNR-Lab\game-check.ps1 'PBZ SB-Win64 Onimusha Magpie'
if($LASTEXITCODE -ne 1){throw 'Game active/check failed'}
$held=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {
 if(Test-Path $r){throw 'Backup exists'}
 if((Get-PSDrive D).Free -lt 100GB){throw 'Low disk'}
 if((Get-FileHash D:\DLSSNR-Lab\pbz-entry-fix.addon64).Hash.ToLower() -ne 'c762cf36d2a12bceca8750fbcc42fa0e1f8a2aecb2084bbcaf3f259a4e0c7347'){throw 'Source hash mismatch'}
 New-Item -ItemType Directory -Force $r|Out-Null
 Copy-Item "$g\dlss5-amd.addon64" "$r\original.addon64"
 Copy-Item "$g\DLSS5-AMD\logs" "$r\logs" -Recurse
 Copy-Item "$g\OptiScaler.log","$g\OptiScaler.ini" $r
 Copy-Item D:\DLSSNR-Lab\pbz-entry-fix.addon64 "$g\dlss5-amd.addon64" -Force
 if((Get-FileHash "$g\dlss5-amd.addon64").Hash.ToLower() -ne 'c762cf36d2a12bceca8750fbcc42fa0e1f8a2aecb2084bbcaf3f259a4e0c7347'){Copy-Item "$r\original.addon64" "$g\dlss5-amd.addon64" -Force;throw 'Readback mismatch'}
 'ENTRY_FIX_INSTALLED'
 Get-FileHash "$g\dlss5-amd.addon64"
}finally{$held.Dispose();Remove-Item D:\DLSSNR-Lab\gpu.lock;'LOCK_RELEASED'}
