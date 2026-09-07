param([string]$Folder='D:\DLSSNR-Lab\native-temporal-network70')
$ErrorActionPreference='Stop'
$Exe=Join-Path $Folder 'native-network70-temporal.exe'
if(Get-Process native-network70-temporal -ErrorAction SilentlyContinue){throw 'Existing full-network test; inspect it instead of restarting'}
$Noise='D:\DLSSNR-Lab\matrix-probe\native-runtime-rgb512\functions.f32'
foreach($Name in 'DLSS5_POST_BASE_ONLY','DLSS5_ALTERNATE_RGB'){Remove-Item "Env:$Name" -ErrorAction SilentlyContinue}
$env:DLSS5_TEST_TEMPORAL_HISTORY=Join-Path $Folder 'history.f32'
$env:DLSS5_TEST_TEMPORAL_MOTION=Join-Path $Folder 'motion.f32'
$env:DLSS5_TEST_RECIPROCAL_TABLE=Join-Path $Folder 'normalized-output.f32'
$env:DLSS5_TEST_TEMPORAL_ORACLE=Join-Path $Folder 'oracle-temporal.f32'
$env:DLSS5_SHADER_PROGRESS='1'
foreach($Item in @(@($Noise,201326592),@($env:DLSS5_TEST_TEMPORAL_HISTORY,33177600),@($env:DLSS5_TEST_TEMPORAL_MOTION,33177600),@($env:DLSS5_TEST_RECIPROCAL_TABLE,33554432),@($env:DLSS5_TEST_TEMPORAL_ORACLE,26542080))){
 if((Get-Item $Item[0]).Length -ne $Item[1]){throw "Fixture size mismatch: $($Item[0])"}
}
$Process=Start-Process -FilePath $Exe -ArgumentList @($Folder,$Noise) -WorkingDirectory $Folder -PassThru -RedirectStandardOutput (Join-Path $Folder 'network.stdout.log') -RedirectStandardError (Join-Path $Folder 'network.stderr.log')
@{pid=$Process.Id;started=$Process.StartTime.ToString('o');executable=$Exe;scope='controlled full network off/on/reset test, not game acceptance'} | ConvertTo-Json | Set-Content (Join-Path $Folder 'run.json')
Write-Output "Started PID=$($Process.Id); inspect this process and network logs, do not restart while alive."
$Process.WaitForExit()
$Process.Refresh()
Write-Output "Finished PID=$($Process.Id) exit=$($Process.ExitCode)"
exit $Process.ExitCode
