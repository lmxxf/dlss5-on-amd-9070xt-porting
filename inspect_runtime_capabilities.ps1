$ErrorActionPreference = 'Stop'
$Developer = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' -ErrorAction SilentlyContinue
$Game = Get-Process -Name 'SB-Win64-Shipping' -ErrorAction SilentlyContinue | Select-Object -First 1
$Modules = @()
if ($Game) {
    $Modules = @($Game.Modules | Where-Object { $_.ModuleName -in @('D3D12.dll','D3D12Core.dll','DirectML.dll','dxgi.dll') } | ForEach-Object {
        [pscustomobject]@{name=$_.ModuleName;path=$_.FileName;version=$_.FileVersionInfo.FileVersion}
    })
}
[pscustomobject]@{
    developer_mode=$Developer.AllowDevelopmentWithoutDevLicense
    os=(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber)
    video=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,DriverDate)
    game_pid=if($Game){$Game.Id}else{$null}
    modules=$Modules
} | ConvertTo-Json -Depth 5
