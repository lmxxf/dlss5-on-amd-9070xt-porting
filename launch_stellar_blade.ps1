$ErrorActionPreference = 'Stop'
$env:DLSS5_MAX_BLOCK = '69'
$Steam = 'C:\Program Files (x86)\Steam\steam.exe'
if (-not (Test-Path -LiteralPath $Steam -PathType Leaf)) {
    throw "Missing Steam executable: $Steam"
}
Start-Process -FilePath $Steam -ArgumentList @(
    '-applaunch', '3489700'
)
