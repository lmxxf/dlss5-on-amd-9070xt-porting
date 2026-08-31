param(
    [ValidateSet('Status', 'Evaluation', 'Enforce')]
    [string]$Mode = 'Status',

    [string]$LabRoot = 'D:\DLSSNR-Lab'
)

$ErrorActionPreference = 'Stop'
$RegistryPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy'
$ValueName = 'VerifiedAndReputablePolicyState'
$StateFile = Join-Path $LabRoot 'smart-app-control-original.json'
$ModeValues = @{ Evaluation = 2; Enforce = 1 }

$current = (Get-ItemProperty -LiteralPath $RegistryPath -Name $ValueName).$ValueName
if (-not (Test-Path -LiteralPath $StateFile -PathType Leaf)) {
    [ordered]@{
        captured_at = (Get-Date).ToString('o')
        original_value = $current
    } | ConvertTo-Json | Set-Content -LiteralPath $StateFile -Encoding UTF8
}

if ($Mode -ne 'Status') {
    $target = $ModeValues[$Mode]
    Set-ItemProperty -LiteralPath $RegistryPath -Name $ValueName -Type DWord -Value $target
    & "$env:WINDIR\System32\CiTool.exe" -r | Out-Null
    Start-Sleep -Seconds 2
    $current = (Get-ItemProperty -LiteralPath $RegistryPath -Name $ValueName).$ValueName
}

[ordered]@{
    requested_mode = $Mode
    current_value = $current
    meaning = switch ($current) {
        0 { 'Off' }
        1 { 'Enforce' }
        2 { 'Evaluation' }
        default { 'Unknown' }
    }
    original_state_file = $StateFile
    active_policies = @(& "$env:WINDIR\System32\CiTool.exe" -lp -json | ConvertFrom-Json)
} | ConvertTo-Json -Depth 10
