param([string]$Lab='D:\DLSSNR-Lab',[ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$RunTag='original')
$ErrorActionPreference='Stop'
$Root=Join-Path $Lab 'matrix-probe'
$RunRoot=if($RunTag -eq 'original'){$Root}else{Join-Path $Root "driver-install-$RunTag"}
$StatusPath=Join-Path $RunRoot 'driver-install-status.json'
if(Test-Path -LiteralPath $StatusPath){throw 'Installation already attempted; inspect status before any retry'}
$Setup=Join-Path $Root 'preview-driver\Setup.exe'
$Signature=Get-AuthenticodeSignature -LiteralPath $Setup
if($Signature.Status -ne 'Valid' -or $Signature.SignerCertificate.Subject -notlike '*Advanced Micro Devices*'){throw 'Setup signature not verified'}
$Before=Get-CimInstance Win32_PnPSignedDriver | Where-Object {$_.DeviceName -eq 'AMD Radeon RX 9070 XT'}
if(@($Before).Count -ne 1 -or $Before.DriverVersion -ne '32.0.31041.1004'){throw 'Driver changed since backup; inspect before proceeding'}
if(-not(Test-Path -LiteralPath (Join-Path $Root 'driver-backup\32.0.31041.1004\u0203304.inf'))){throw 'Rollback INF missing'}
if(Get-Process SB-Win64-Shipping,native-network70-temporal,Setup -ErrorAction SilentlyContinue){throw 'Game/test/installer running; inspect first'}
New-Item -ItemType Directory -Force $RunRoot | Out-Null
$State=[ordered]@{phase='launching';started_utc=[DateTime]::UtcNow.ToString('o');previous_version=$Before.DriverVersion;target_version='32.0.31007.2048';auto_reboot=$false;pid=$null;exit_code=$null}
$State | ConvertTo-Json | Set-Content -LiteralPath $StatusPath -Encoding UTF8
try{
    # AMD documents -boot as the opt-in auto-reboot switch. Do not pass it or cleanup/factory-reset options.
    $Process=Start-Process -FilePath $Setup -WorkingDirectory (Split-Path $Setup) -ArgumentList @('-INSTALL','-OUTPUT',(Join-Path $RunRoot 'driver-install-output.log'),'-LOG',(Join-Path $RunRoot 'driver-install-result.log')) -PassThru
    $null=$Process.Handle
    $State.phase='running';$State.pid=$Process.Id
    $State | ConvertTo-Json | Set-Content -LiteralPath $StatusPath -Encoding UTF8
    $Process.WaitForExit();$Process.Refresh()
    $State.phase='root_exited';$State.exit_code=$Process.ExitCode
    $State.finished_utc=[DateTime]::UtcNow.ToString('o')
    $State | ConvertTo-Json | Set-Content -LiteralPath $StatusPath -Encoding UTF8
}catch{
    $State.phase='failed';$State.error=$_.Exception.Message
    $State | ConvertTo-Json | Set-Content -LiteralPath $StatusPath -Encoding UTF8
    throw
}
