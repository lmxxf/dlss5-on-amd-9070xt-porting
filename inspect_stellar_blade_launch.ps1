$Steam = 'C:\Program Files (x86)\Steam'
Write-Output 'gameprocess_log:'
Get-Content -LiteralPath (Join-Path $Steam 'logs\gameprocess_log.txt') -Tail 120 -ErrorAction SilentlyContinue
Write-Output 'console_log:'
Get-Content -LiteralPath (Join-Path $Steam 'logs\console_log.txt') -Tail 100 -ErrorAction SilentlyContinue
Write-Output 'recent_application_errors:'
Get-WinEvent -FilterHashtable @{ LogName = 'Application'; StartTime = (Get-Date).AddMinutes(-15) } -ErrorAction SilentlyContinue |
    Where-Object { $_.LevelDisplayName -in @('Error', 'Critical') } |
    Select-Object -First 12 TimeCreated, ProviderName, Id, Message |
    Format-List
Write-Output 'recent_system_gpu_events:'
Get-WinEvent -FilterHashtable @{ LogName = 'System'; StartTime = (Get-Date).AddMinutes(-15) } -ErrorAction SilentlyContinue |
    Where-Object { $_.LevelDisplayName -in @('Error', 'Critical', 'Warning') -or $_.ProviderName -match 'Display|amdw|dxg' } |
    Select-Object -First 20 TimeCreated, ProviderName, Id, LevelDisplayName, Message |
    Format-List
