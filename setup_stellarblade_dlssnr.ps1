param(
    [ValidateSet('Status', 'Install', 'Restore')]
    [string]$Action = 'Status',

    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\StellarBlade',

    [string]$LabRoot = 'D:\DLSSNR-Lab'
)

$ErrorActionPreference = 'Stop'
$Target = Join-Path $GameRoot 'SB\Binaries\Win64'
$TargetExe = Join-Path $Target 'SB-Win64-Shipping.exe'
$Tools = Join-Path $LabRoot 'tools'
$Nvidia = Join-Path $LabRoot 'nvidia'
$Backup = Join-Path $LabRoot 'backup-stellarblade'
$ManifestPath = Join-Path $Backup 'manifest.json'

$ManagedFiles = @(
    'dxgi.dll',
    'd3d12.dll',
    'ReShade.ini',
    'ReShade.log',
    'renodx-dlss5.addon64',
    'nvngx_dlssnr.dll'
)

$ExpectedHashes = @{
    'renodx-dlss5.addon64' = 'E1C28FDE0922B12FC10734E58C3D24A36808E575247F4FD4F36226540D7EE023'
    'nvngx_dlssnr.dll' = 'E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E'
    'ReShade_Setup_6.8.0_Addon.exe' = 'AFE4C8F13048306307983B8B3D41D5BF00A86820440B0E57DEA10950E1176445'
}

function Assert-Hash([string]$Path, [string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing required file: $Path"
    }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actual -ne $Expected) {
        throw "SHA-256 mismatch for $Path`nExpected: $Expected`nActual:   $actual"
    }
}

function Get-StatusDocument {
    $files = foreach ($name in $ManagedFiles) {
        $path = Join-Path $Target $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $item = Get-Item -LiteralPath $path
            [ordered]@{
                name = $name
                exists = $true
                size = $item.Length
                sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
            }
        }
        else {
            [ordered]@{ name = $name; exists = $false }
        }
    }
    return [ordered]@{
        target = $Target
        executable_exists = Test-Path -LiteralPath $TargetExe -PathType Leaf
        manifest_exists = Test-Path -LiteralPath $ManifestPath -PathType Leaf
        files = @($files)
    }
}

if ($Action -eq 'Status') {
    Get-StatusDocument | ConvertTo-Json -Depth 6
    exit 0
}

if ($Action -eq 'Install') {
    if (-not (Test-Path -LiteralPath $TargetExe -PathType Leaf)) {
        throw "Game executable not found: $TargetExe"
    }
    New-Item -ItemType Directory -Force -Path $Backup | Out-Null

    $manifestEntries = foreach ($name in $ManagedFiles) {
        $source = Join-Path $Target $name
        $backupFile = Join-Path $Backup $name
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination $backupFile -Force
            [ordered]@{ name = $name; originally_existed = $true; backup = $backupFile }
        }
        else {
            [ordered]@{ name = $name; originally_existed = $false; backup = $null }
        }
    }
    [ordered]@{
        created_at = (Get-Date).ToString('o')
        target = $Target
        files = @($manifestEntries)
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ManifestPath -Encoding UTF8

    $addon = Join-Path $Tools 'renodx-dlss5.addon64'
    $runtime = Join-Path $Nvidia 'nvngx_dlssnr.dll'
    $setup = Join-Path $Tools 'ReShade_Setup_6.8.0_Addon.exe'
    Assert-Hash $addon $ExpectedHashes['renodx-dlss5.addon64']
    Assert-Hash $runtime $ExpectedHashes['nvngx_dlssnr.dll']
    Assert-Hash $setup $ExpectedHashes['ReShade_Setup_6.8.0_Addon.exe']

    $process = Start-Process -FilePath $setup -ArgumentList @(
        '--headless', '--api', 'dxgi', $TargetExe
    ) -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "ReShade setup failed with exit code $($process.ExitCode)"
    }

    $dxgi = Join-Path $Target 'dxgi.dll'
    $d3d12 = Join-Path $Target 'd3d12.dll'
    if ((Test-Path -LiteralPath $dxgi) -and (-not (Test-Path -LiteralPath $d3d12))) {
        Move-Item -LiteralPath $dxgi -Destination $d3d12
    }

    Copy-Item -LiteralPath $addon -Destination (Join-Path $Target 'renodx-dlss5.addon64') -Force
    Copy-Item -LiteralPath $runtime -Destination (Join-Path $Target 'nvngx_dlssnr.dll') -Force
    Copy-Item -LiteralPath (Join-Path $Tools 'ReShade.ini') -Destination (Join-Path $Target 'ReShade.ini') -Force

    Get-StatusDocument | ConvertTo-Json -Depth 6
    exit 0
}

if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw "Restore manifest not found: $ManifestPath"
}
$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
foreach ($entry in $manifest.files) {
    $targetFile = Join-Path $Target $entry.name
    if ($entry.originally_existed) {
        Copy-Item -LiteralPath $entry.backup -Destination $targetFile -Force
    }
    elseif (Test-Path -LiteralPath $targetFile -PathType Leaf) {
        Remove-Item -LiteralPath $targetFile -Force
    }
}
Get-StatusDocument | ConvertTo-Json -Depth 6
