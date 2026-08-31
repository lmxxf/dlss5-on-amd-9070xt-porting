param(
    [string]$Needle = 'renodx-dlss5.addon64'
)

$ErrorActionPreference = 'Stop'
$events = Get-WinEvent -FilterHashtable @{
    LogName = 'Microsoft-Windows-CodeIntegrity/Operational'
    StartTime = (Get-Date).AddHours(-2)
} -ErrorAction SilentlyContinue |
    Where-Object { $_.Message -like "*$Needle*" } |
    Select-Object -First 20 Id, TimeCreated, LevelDisplayName, Message

$smartAppControl = Get-ItemProperty `
    'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' `
    -ErrorAction SilentlyContinue

$zoneStreams = Get-Item `
    'D:\SteamLibrary\steamapps\common\StellarBlade\SB\Binaries\Win64\renodx-dlss5.addon64' `
    -Stream * `
    -ErrorAction SilentlyContinue |
    Select-Object Stream, Length

[ordered]@{
    smart_app_control = [ordered]@{
        verified_and_reputable_policy_state = $smartAppControl.VerifiedAndReputablePolicyState
        was_enabled_by = $smartAppControl.WasEnabledBy
    }
    zone_streams = @($zoneStreams)
    code_integrity_events = @($events)
} | ConvertTo-Json -Depth 8
