param([string]$Folder='D:\DLSSNR-Lab\native-color-frame-test')
$ErrorActionPreference='Stop'
if(Get-Process native-game-frame-test -ErrorAction SilentlyContinue){throw 'Existing frame test; inspect it instead of restarting'}
$Manifest=Get-Content (Join-Path $Folder 'shader-manifest.json') -Raw | ConvertFrom-Json
if($Manifest.Count -ne 23){throw 'Incomplete color shader manifest'}
foreach($Entry in $Manifest){
 if([IO.Path]::GetFileName($Entry.name) -ne $Entry.name){throw 'Expected basename'}
 if((Get-FileHash (Join-Path $Folder $Entry.name) -Algorithm SHA256).Hash -ne $Entry.sha256){throw "Shader mismatch: $($Entry.name)"}
}
$Expected=Join-Path $Folder 'expected.f16'
if((Get-FileHash $Expected -Algorithm SHA256).Hash -ne 'e3c82de76e428a682780522b1147650bee5e80ce8ab26bbbe1a4d31712b71535'){throw 'Wrong original frame oracle'}
foreach($Name in 'source.f16','expected.f16'){if((Get-Item (Join-Path $Folder $Name)).Length -ne 16588800){throw 'Frame fixture size'}}
foreach($Name in 'DLSS5_POST_BASE_ONLY','DLSS5_ALTERNATE_RGB'){Remove-Item "Env:$Name" -ErrorAction SilentlyContinue}
$env:DLSS5_SHADER_PROGRESS='1'
$Exe=Join-Path $Folder 'native-game-frame-test.exe'
$Noise='D:\DLSSNR-Lab\matrix-probe\native-runtime-rgb512\functions.f32'
$Process=Start-Process $Exe -ArgumentList @($Folder,$Noise,$Folder) -WorkingDirectory $Folder -PassThru -RedirectStandardOutput (Join-Path $Folder 'frame.stdout.log') -RedirectStandardError (Join-Path $Folder 'frame.stderr.log')
$null=$Process.Handle
@{pid=$Process.Id;started=$Process.StartTime.ToString('o');exe_sha256=(Get-FileHash $Exe).Hash;source_sha256=(Get-FileHash (Join-Path $Folder 'source.f16')).Hash;scope='fixed first-frame full color chain, not game acceptance'} | ConvertTo-Json | Set-Content (Join-Path $Folder 'frame-run.json')
Write-Output "Started PID=$($Process.Id)"
$Process.WaitForExit()
if($null -eq $Process.ExitCode){throw 'No exit code; inspect process and logs'}
Write-Output "Finished PID=$($Process.Id) exit=$($Process.ExitCode)"
exit $Process.ExitCode
