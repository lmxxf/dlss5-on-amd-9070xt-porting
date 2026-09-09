param([int]$TimeoutSeconds = 180)
$ErrorActionPreference = 'Stop'
$Lab = 'D:\DLSSNR-Lab'
$Deploy = Join-Path $Lab 'deploy_resident_lifecycle_probe.ps1'
$Log = Join-Path $Lab 'logs\resident-lifecycle-probe.txt'
$SteamUri = 'steam://rungameid/3489700'

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Deploy -Action Install | Write-Output
if ($LASTEXITCODE) { throw "Probe installation failed: $LASTEXITCODE" }

if (-not (Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue)) {
    Start-Process $SteamUri
    Write-Output 'Stellar Blade launch requested through the interactive Steam session.'
}

$Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
do {
    Start-Sleep -Seconds 2
    if (Test-Path -LiteralPath $Log -PathType Leaf) {
        $Text = Get-Content -LiteralPath $Log -Raw
        if ($Text -match 'resident_ready') {
            [ordered]@{
                passed = $true
                game_running = [bool](Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue)
                log = $Text
            } | ConvertTo-Json
            exit 0
        }
        if ($Text -match 'resident_failed|resident_execution_failed|resident_operator_failed') {
            [ordered]@{ passed = $false; reason = 'probe reported failure'; log = $Text } | ConvertTo-Json
            exit 2
        }
    }
} while ([DateTime]::UtcNow -lt $Deadline)

$Status = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Deploy -Action Status | ConvertFrom-Json
[ordered]@{
    passed = $false
    reason = 'timeout waiting for resident_ready'
    timeout_seconds = $TimeoutSeconds
    game_running = $Status.game_running
    probe_installed = $Status.installed
    log = $Status.log
} | ConvertTo-Json
exit 3
