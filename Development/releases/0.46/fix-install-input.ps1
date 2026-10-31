$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\release-046';$g='D:\DLSSNR-Lab\game-install-20261031'
if(Test-Path D:\DLSSNR-Lab\gpu.lock){throw 'busy'}
foreach($n in 'failure.json','backup-manifest.json','install-plan.json','config-before.json','rollback.ps1'){Copy-Item "$g\$n" "$g\attempt1-$n"}
$utf8=[Text.UTF8Encoding]::new($false)
foreach($arch in 'gfx1200','gfx1201'){$rows=@(Get-ChildItem "$r\HIP\$arch" -Filter *.hsaco|Sort-Object Name|ForEach-Object{(Get-FileHash $_.FullName).Hash.ToLower()+'  '+$_.Name});[IO.File]::WriteAllLines("$r\HIP\$arch\SHA256SUMS",$rows,$utf8)}
$x=@(Get-Content "$r\payload-expected.json" -Raw -Encoding UTF8|ConvertFrom-Json)
foreach($arch in 'gfx1200','gfx1201'){$x+=@{relative="HIP/$arch/SHA256SUMS";sha256=(Get-FileHash "$r\HIP\$arch\SHA256SUMS").Hash.ToLower()}}
$x|ConvertTo-Json|Set-Content "$r\payload-expected.json" -Encoding UTF8
