$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend'
foreach($kind in 1,2){
 $work="$r\vit-adaptive-image-schedule-$kind-smoke-results";$rows=@()
 foreach($seq in 0,1,6){foreach($i in 0..11){$a=(Get-FileHash "$work\base-900-s$seq\rgb-frame-$i.f16").Hash;$b=(Get-FileHash "$work\adaptive-900-s$seq\rgb-frame-$i.f16").Hash;$rows+=[pscustomobject]@{kind=$kind;sequence=$seq;frame=$i;base_sha=$a;candidate_sha=$b;identical=($a -eq $b)}}}
 $rows|Export-Csv "$work\identity.csv" -NoTypeInformation
 if(@($rows|Where-Object{!$_.identical}).Count){throw 'Identity mismatch'}
 foreach($suffix in 'smoke','timing'){& "$r\vit-adaptive-collect.ps1" -Tag "image-schedule-$kind-$suffix"}
}
& "$r\vit-adaptive-collect.ps1" -Tag image-span
