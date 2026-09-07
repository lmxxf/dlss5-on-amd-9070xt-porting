Get-ScheduledTask | Where-Object { $_.TaskName -match 'AMD|Ati|Radeon' -or $_.TaskPath -match 'AMD' } | ForEach-Object {
 $i=$_ | Get-ScheduledTaskInfo
 '{0}{1} | last={2} | result={3} | state={4}' -f $_.TaskPath,$_.TaskName,$i.LastRunTime,$i.LastTaskResult,$_.State
}
'---- system events 03:50-04:30'
Get-WinEvent -FilterHashtable @{LogName='System';StartTime=(Get-Date '2026-09-08 03:50');EndTime=(Get-Date '2026-09-08 04:30')} |
 Where-Object { $_.Id -in 41,1074,6005,6006,6008,12,13,7045,4101,7036 -and $_.Message -notmatch 'entered the stopped|entered the running' } |
 Select-Object TimeCreated,Id,ProviderName | Format-Table -AutoSize
'---- security logons 04:00-04:25'
Get-WinEvent -FilterHashtable @{LogName='Security';Id=4624;StartTime=(Get-Date '2026-09-08 04:00');EndTime=(Get-Date '2026-09-08 04:25')} -ErrorAction SilentlyContinue |
 ForEach-Object { $x=[xml]$_.ToXml(); $d=@{}; $x.Event.EventData.Data | ForEach-Object { $d[$_.Name]=$_.'#text' }; '{0} type={1} user={2} proc={3}' -f $_.TimeCreated,$d.LogonType,$d.TargetUserName,$d.ProcessName } | Select-Object -First 15
'---- AMD installer processes (event 4688) 04:15-04:25'
Get-WinEvent -FilterHashtable @{LogName='Security';Id=4688;StartTime=(Get-Date '2026-09-08 04:15');EndTime=(Get-Date '2026-09-08 04:25')} -ErrorAction SilentlyContinue |
 Where-Object { $_.Message -match 'AMD|Ati' } | Select-Object TimeCreated -ExpandProperty Message | Select-Object -First 3
