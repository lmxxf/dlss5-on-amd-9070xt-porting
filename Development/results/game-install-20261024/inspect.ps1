$ErrorActionPreference='Stop'
$games=@('C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64','C:\XboxGames\Onimusha- Way of the Sword\Content')
$g=@();foreach($root in $games){$cfg=@();foreach($n in 'default-config.txt','custom-config.txt','native-game-flags.txt'){$p="$root\DLSS5-AMD\$n";$cfg+=@{name=$n;exists=(Test-Path -LiteralPath $p);sha256=$(if(Test-Path -LiteralPath $p){(Get-FileHash -LiteralPath $p).Hash.ToLower()})}}
$g+=@{root=$root;configs=$cfg;storage_mod_assets=(Test-Path "$root\_storage_\DLSS5-AMD");mod_bytes=(Get-ChildItem "$root\DLSS5-AMD" -File -Recurse|Measure-Object Length -Sum).Sum}}
@{games=$g;out=@(Get-ChildItem -LiteralPath 'D:\給網友打包'|Select-Object Name,Length,FullName);disks=@(Get-PSDrive C,D|Select-Object Name,Free);smoke=@(Get-ChildItem D:\DLSSNR-Lab\release-044* -Filter runtime-smoke.exe -Recurse|Select-Object FullName,Length)}|ConvertTo-Json -Depth 6
