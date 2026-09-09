foreach($n in 'AMD Install Manager - Check For Updates','AMD Install Manager - Install Updates'){
 $t=Get-ScheduledTask -TaskName $n
 "== $n  state=$($t.State)  user=$($t.Principal.UserId) run=$($t.Principal.RunLevel)"
 $t.Triggers | ForEach-Object { '  trigger: {0} start={1} repeat={2} enabled={3}' -f $_.CimClass.CimClassName,$_.StartBoundary,$_.Repetition.Interval,$_.Enabled }
 $t.Actions | ForEach-Object { '  action: {0} {1}' -f $_.Execute,$_.Arguments }
}
'---- task history (Operational log) 04:15-04:30'
Get-WinEvent -FilterHashtable @{LogName='Microsoft-Windows-TaskScheduler/Operational';StartTime=(Get-Date '2026-09-08 04:15');EndTime=(Get-Date '2026-09-08 04:30')} -ErrorAction SilentlyContinue | Where-Object {$_.Message -match 'AMD'} | Select-Object TimeCreated,Id -ExpandProperty Message | Select-Object -First 8
'---- current display driver'
Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,DriverDate | Format-Table -AutoSize
