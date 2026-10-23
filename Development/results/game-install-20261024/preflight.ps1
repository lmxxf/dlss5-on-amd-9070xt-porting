$ErrorActionPreference='Stop'
$games=@('C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64','C:\XboxGames\Onimusha- Way of the Sword\Content')
& D:\DLSSNR-Lab\game-check.ps1 'Shipping SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game busy'}
if(Test-Path D:\DLSSNR-Lab\gpu.lock){throw 'gpu lock present'}
$busy=@(Get-Process|Where-Object {$_.ProcessName -match '^(frame|pure|benchmark|rtc_compile.*|runtime-smoke)$'});if($busy.Count){throw 'experiment busy'}
foreach($g in $games){"GAME $g";Get-ChildItem -LiteralPath $g -File|Select-Object Name,Length;Get-ChildItem -LiteralPath "$g\_storage_" -ErrorAction SilentlyContinue|Select-Object Name,Length
foreach($n in 'default-config.txt','custom-config.txt','native-game-flags.txt'){$p="$g\DLSS5-AMD\$n";if(Test-Path -LiteralPath $p){[pscustomobject]@{config=$p;sha256=(Get-FileHash -LiteralPath $p).Hash}}}
"HIP_COUNT $(@(Get-ChildItem -LiteralPath "$g\DLSS5-AMD\native-game-tiled-assets\HIP" -Recurse -Filter *.hsaco).Count)"
}
Get-ChildItem -LiteralPath 'D:\給網友打包' -Filter '*0.44*'|Select-Object Name,Length
Get-PSDrive C,D|Select-Object Name,Free
