param(
    [ValidateSet('Status', 'Install', 'Remove')]
    [string]$Action = 'Status'
)

$ErrorActionPreference = 'Stop'
$Source = 'D:\DLSSNR-Lab\tools\dlssnr-runtime-probe.addon64'
$Target = 'D:\SteamLibrary\steamapps\common\StellarBlade\SB\Binaries\Win64\dlssnr-runtime-probe.addon64'
$Dump = 'D:\DLSSNR-Lab\logs\runtime-network.txt'
$ExpectedHash = '5C03355B2CDF62B2BDBA0B3B4E4445750BC44B82E77D8CADB4D115A01BAA1188'

if ($Action -eq 'Status') {
    [ordered]@{
        probe_exists = Test-Path -LiteralPath $Target -PathType Leaf
        probe_hash = if (Test-Path -LiteralPath $Target -PathType Leaf) {
            (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash
        } else { $null }
        dump_exists = Test-Path -LiteralPath $Dump -PathType Leaf
        dump_size = if (Test-Path -LiteralPath $Dump -PathType Leaf) {
            (Get-Item -LiteralPath $Dump).Length
        } else { $null }
    } | ConvertTo-Json
    exit 0
}

if ($Action -eq 'Remove') {
    Remove-Item -LiteralPath $Target -Force -ErrorAction SilentlyContinue
    Write-Output 'DLSSNR runtime probe removed.'
    exit 0
}

if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    throw "Probe source missing: $Source"
}
$hash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
if ($hash -ne $ExpectedHash) {
    throw "Probe SHA-256 mismatch. Expected $ExpectedHash, got $hash"
}
Remove-Item -LiteralPath $Dump -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $Source -Destination $Target -Force
Write-Output 'DLSSNR runtime probe installed.'
