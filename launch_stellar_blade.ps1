$ErrorActionPreference = 'Stop'
$Game = 'C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
$Executable = Join-Path $Game 'SB-Win64-Shipping.exe'
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Missing Stellar Blade executable: $Executable"
}
Start-Process -FilePath $Executable -WorkingDirectory $Game
