param(
    [ValidateSet('Status', 'Install', 'Remove')]
    [string]$Action = 'Status'
)
$ErrorActionPreference = 'Stop'
$Lab = 'D:\DLSSNR-Lab'
$Game = 'C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
$Source = Join-Path $Lab 'dlss5-1080p-runtime.addon64'
$Target = Join-Path $Game 'dlss5-1080p-runtime.addon64'
$Log = Join-Path $Lab 'logs\dlss5-1080p-runtime.txt'
$ExpectedHash = 'C48E77D01A0515DE6230015D2B7BCB0C40E52F7AE236569BF0FD1CC5528E5866'
$OldAddons = @(
    (Join-Path $Game 'd3d12-dynamic-resource-probe.addon64'),
    (Join-Path $Game 'dlss5-resident-lifecycle.addon64')
)

if ($Action -eq 'Install') {
    if (Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue) {
        throw 'Refusing to replace runtime add-ons while Stellar Blade is running.'
    }
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Missing runtime: $Source" }
    $Hash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    if ($Hash -ne $ExpectedHash) { throw "Runtime SHA mismatch: $Hash" }
    foreach ($Old in $OldAddons) { Remove-Item -LiteralPath $Old -Force -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath $Log -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $Source -Destination $Target -Force
} elseif ($Action -eq 'Remove') {
    if (Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue) {
        throw 'Refusing to remove runtime while Stellar Blade is running.'
    }
    Remove-Item -LiteralPath $Target -Force -ErrorAction SilentlyContinue
}

$Installed = Test-Path -LiteralPath $Target -PathType Leaf
[ordered]@{
    installed = $Installed
    installed_hash = if ($Installed) { (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash } else { $null }
    expected_hash = $ExpectedHash
    conflicting_addons = @($OldAddons | Where-Object { Test-Path -LiteralPath $_ })
    game_running = [bool](Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue)
    log_exists = Test-Path -LiteralPath $Log -PathType Leaf
    log = if (Test-Path -LiteralPath $Log -PathType Leaf) { Get-Content -LiteralPath $Log -Raw } else { $null }
} | ConvertTo-Json
