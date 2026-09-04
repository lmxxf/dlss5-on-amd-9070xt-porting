$ErrorActionPreference = 'Stop'
$Steam = 'C:\Program Files (x86)\Steam\steam.exe'
if (-not (Test-Path -LiteralPath $Steam -PathType Leaf)) {
    throw "Missing Steam executable: $Steam"
}
Start-Process -FilePath $Steam -ArgumentList @(
    '-applaunch', '3489700',
    '-ResX=1920', '-ResY=1080', '-Fullscreen'
)
