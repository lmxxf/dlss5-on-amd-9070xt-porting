param([string]$op,[string]$name='c256-gap')
$L='D:\DLSSNR-Lab\gpu.lock'
if($op -eq 'take'){
 $t0=Get-Date
 while(Test-Path $L){
  $age=((Get-Date)-(Get-Item $L).LastWriteTime).TotalMinutes
  if($age -gt 40){Remove-Item $L -Force;"stale lock removed";break}
  if(((Get-Date)-$t0).TotalMinutes -gt 30){"LOCK TIMEOUT";exit 1}
  Start-Sleep 60
 }
 "$name $(Get-Date -Format s)" | Out-File -Encoding ascii $L; "locked"
} elseif($op -eq 'drop'){ if((Test-Path $L) -and ((Get-Content $L) -match $name)){Remove-Item $L -Force}; "unlocked" }
