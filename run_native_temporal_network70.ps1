param([string]$Folder='D:\DLSSNR-Lab\native-temporal-network70',[ValidateRange(0,3)][int]$PostShift=0,[switch]$SingleList,[switch]$GpuProfile)
$ErrorActionPreference='Stop'
if($SingleList -and $GpuProfile){throw 'Timestamp reporting requires completed segmented submissions'}
if($GpuProfile){$env:DLSS5_NETWORK_GPU_PROFILE='1'}else{Remove-Item Env:DLSS5_NETWORK_GPU_PROFILE -ErrorAction SilentlyContinue}
$Exe=Join-Path $Folder 'native-network70-temporal.exe'
foreach($Map in 'hwc-to-vit.i32','vit-to-hwc.i32'){
 if(!(Test-Path (Join-Path $Folder $Map) -PathType Leaf)){throw "Missing layout map: $Map"}
}
if(Get-Process native-network70-temporal -ErrorAction SilentlyContinue){throw 'Existing full-network test; inspect it instead of restarting'}
$Manifest=Get-Content (Join-Path $Folder 'shader-manifest.json') -Raw | ConvertFrom-Json
if($Manifest.Count -ne 19){throw 'Incomplete shader manifest'}
foreach($Entry in $Manifest){
 if([IO.Path]::GetFileName($Entry.name) -ne $Entry.name){throw 'Manifest must contain basenames'}
 if((Get-FileHash (Join-Path $Folder $Entry.name) -Algorithm SHA256).Hash -ne $Entry.sha256){throw "Shader mismatch: $($Entry.name)"}
}
if(!(Select-String -Quiet -Path (Join-Path $Folder 'preblock_input_mix.hlsl') -Pattern 'NATIVE_TEMPORAL_RGB')){throw 'Temporal input shader missing'}
$Noise='D:\DLSSNR-Lab\matrix-probe\native-runtime-rgb512\functions.f32'
foreach($Name in 'DLSS5_POST_BASE_ONLY','DLSS5_ALTERNATE_RGB'){Remove-Item "Env:$Name" -ErrorAction SilentlyContinue}
$env:DLSS5_TEST_TEMPORAL_HISTORY=Join-Path $Folder 'history.f32'
$env:DLSS5_TEST_TEMPORAL_MOTION=Join-Path $Folder 'motion.f32'
$env:DLSS5_TEST_RECIPROCAL_TABLE=Join-Path $Folder 'normalized-output.f32'
$env:DLSS5_TEST_TEMPORAL_ORACLE=Join-Path $Folder 'oracle-temporal.f32'
$env:DLSS5_SHADER_PROGRESS='1'
$env:DLSS5_TEST_POST_SHIFT=[string]$PostShift
if($SingleList){$env:DLSS5_TEST_SINGLE_LIST='1'}else{Remove-Item Env:DLSS5_TEST_SINGLE_LIST -ErrorAction SilentlyContinue}
foreach($Item in @(@($Noise,201326592),@($env:DLSS5_TEST_TEMPORAL_HISTORY,33177600),@($env:DLSS5_TEST_TEMPORAL_MOTION,33177600),@($env:DLSS5_TEST_RECIPROCAL_TABLE,33554432),@($env:DLSS5_TEST_TEMPORAL_ORACLE,26542080))){
 if((Get-Item $Item[0]).Length -ne $Item[1]){throw "Fixture size mismatch: $($Item[0])"}
}
$Process=Start-Process -FilePath $Exe -ArgumentList @($Folder,$Noise) -WorkingDirectory $Folder -PassThru -RedirectStandardOutput (Join-Path $Folder 'network.stdout.log') -RedirectStandardError (Join-Path $Folder 'network.stderr.log')
$null=$Process.Handle
@{pid=$Process.Id;started=$Process.StartTime.ToString('o');executable=$Exe;post_shift=$PostShift;scope='controlled full network off/on/reset test, not game acceptance'} | ConvertTo-Json | Set-Content (Join-Path $Folder 'run.json')
Write-Output "Started PID=$($Process.Id); inspect this process and network logs, do not restart while alive."
$Process.WaitForExit()
if($null -eq $Process.ExitCode){throw 'Exit code unavailable; inspect test logs, do not assume success'}
Write-Output "Finished PID=$($Process.Id) exit=$($Process.ExitCode)"
exit $Process.ExitCode
