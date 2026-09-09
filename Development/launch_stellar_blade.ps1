$ErrorActionPreference = 'Stop'
$Lab = 'D:\DLSSNR-Lab'
Set-Content -LiteralPath (Join-Path $Lab 'runtime-max-block.txt') -Value '70' -NoNewline -Encoding Ascii
Set-Content -LiteralPath (Join-Path $Lab 'runtime-disable-present.txt') -Value '1' -NoNewline -Encoding Ascii
$Steam = 'C:\Program Files (x86)\Steam\steam.exe'
if (-not (Test-Path -LiteralPath $Steam -PathType Leaf)) {
    throw "Missing Steam executable: $Steam"
}
Start-Process -FilePath $Steam -ArgumentList @(
    '-applaunch', '3489700'
)
