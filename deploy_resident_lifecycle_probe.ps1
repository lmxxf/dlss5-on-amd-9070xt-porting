param(
    [ValidateSet('Status', 'Install', 'Remove')]
    [string]$Action = 'Status'
)

$ErrorActionPreference = 'Stop'
$Source = 'D:\DLSSNR-Lab\dlss5-resident-lifecycle.addon64'
$Target = 'C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64\dlss5-resident-lifecycle.addon64'
$Log = 'D:\DLSSNR-Lab\logs\resident-lifecycle-probe.txt'
$ExpectedHash = '8B2ABE1F31E02D5F3B5E3DB12AB0498EC02181C38410D00539B51492441921BD'

if ($Action -eq 'Remove') {
    Remove-Item -LiteralPath $Target -Force -ErrorAction SilentlyContinue
    Write-Output 'Resident lifecycle probe removed.'
    exit 0
}

if ($Action -eq 'Install') {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Missing probe: $Source" }
    $Hash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    if ($Hash -ne $ExpectedHash) { throw "Probe SHA mismatch: $Hash" }
    Remove-Item -LiteralPath $Log -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $Source -Destination $Target -Force
}

$Installed = Test-Path -LiteralPath $Target -PathType Leaf
[ordered]@{
    installed = $Installed
    installed_hash = if ($Installed) { (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash } else { $null }
    expected_hash = $ExpectedHash
    game_running = [bool](Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue)
    log_exists = Test-Path -LiteralPath $Log -PathType Leaf
    log = if (Test-Path -LiteralPath $Log -PathType Leaf) { Get-Content -LiteralPath $Log -Raw } else { $null }
} | ConvertTo-Json
