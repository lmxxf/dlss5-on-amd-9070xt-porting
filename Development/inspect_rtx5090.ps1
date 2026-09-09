param(
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\StellarBlade',

    [int]$SteamLogTail = 0
)

$ErrorActionPreference = 'Stop'

$dlls = Get-ChildItem -LiteralPath $GameRoot -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like 'nvngx*.dll' -or $_.Name -like 'sl.*.dll' } |
    ForEach-Object {
        [ordered]@{
            path = $_.FullName
            size = $_.Length
            product = $_.VersionInfo.ProductName
            description = $_.VersionInfo.FileDescription
            file_version = $_.VersionInfo.FileVersion
        }
    }

$executables = Get-ChildItem -LiteralPath $GameRoot -Recurse -File -Filter '*.exe' -ErrorAction SilentlyContinue |
    ForEach-Object {
        [ordered]@{
            path = $_.FullName
            size = $_.Length
            product = $_.VersionInfo.ProductName
            file_version = $_.VersionInfo.FileVersion
        }
    }

$tools = 'python', 'py', 'git', 'cmake', 'nvcc', 'dumpbin', 'nsys', 'ncu', 'windbg' |
    ForEach-Object {
        $command = Get-Command $_ -ErrorAction SilentlyContinue
        if ($command) {
            [ordered]@{ name = $_; path = $command.Source }
        }
    }

$driverFiles = 'nvcuda.dll', 'nvapi64.dll' |
    ForEach-Object {
        $path = Join-Path $env:WINDIR "System32\$_"
        if (Test-Path -LiteralPath $path) {
            $item = Get-Item -LiteralPath $path
            [ordered]@{
                path = $item.FullName
                size = $item.Length
                file_version = $item.VersionInfo.FileVersion
            }
        }
    }

$steamLogs = [ordered]@{}
if ($SteamLogTail -gt 0) {
    $steamLogRoot = 'C:\Program Files (x86)\Steam\logs'
    foreach ($name in 'console_log.txt', 'content_log.txt', 'bootstrap_log.txt') {
        $path = Join-Path $steamLogRoot $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $steamLogs[$name] = @(Get-Content -LiteralPath $path -Tail $SteamLogTail)
        }
    }
}

[ordered]@{
    computer = $env:COMPUTERNAME
    user = [Environment]::UserName
    game_root = $GameRoot
    executables = @($executables)
    nvidia_dlls = @($dlls)
    tools = @($tools)
    driver_files = @($driverFiles)
    steam_logs = $steamLogs
} | ConvertTo-Json -Depth 8
