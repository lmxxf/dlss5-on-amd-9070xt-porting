$ErrorActionPreference='Stop'
$g='C:\Program Files (x86)\Steam\steamapps\common\PBZ'
foreach($p in @('platform\steam','steam')) { Get-ChildItem "$g\$p" -File | Select-Object Name,Length,@{n='Directory';e={$_.DirectoryName}} | ConvertTo-Json }
Get-ChildItem "$g\ProjectPBZ\Saved\Logs" -File -ErrorAction SilentlyContinue | Select-Object Name,Length,LastWriteTime | ConvertTo-Json
Get-ChildItem "$g\ProjectPBZ\Plugins" -Recurse -File | Where-Object {$_.Name -match 'nvngx|fidelity|fsr|sl.interposer'} | ForEach-Object {$_.FullName}
Get-ChildItem 'D:\給網友打包\OptiScaler-DLSS5-AMD-0.46' | Select-Object Name,Length | ConvertTo-Json
Get-Content 'D:\給網友打包\OptiScaler-DLSS5-AMD-0.46\OptiScaler.ini' | Select-String 'Path=|Dx12Upscaler|EnableFfx|ReShade'
Get-PSDrive C,D | Select-Object Name,Free | ConvertTo-Json
& D:\DLSSNR-Lab\game-check.ps1 'PBZ SB-Win64 Onimusha Magpie'
