param([string]$Set)
$root='D:\DLSSNR-Lab\hip-backend\c256-gap-20261001'
function P99($v){$s=@($v|Sort-Object);$s[[math]::Ceiling(0.99*$s.Count)-1]}
foreach($h in 900,1080){$b=@();$c=@();foreach($d in Get-ChildItem $root -Directory -Filter "runtime-regression-$Set-timing-*"){foreach($slot in 0..3){$rows=@(Import-Csv "$($d.FullName)\time-$h-$slot\rgb.csv"|Where-Object{[int]$_.frame -ge 200}|ForEach-Object{[double]$_.wall_ms});if($slot -in 1,2){$c+=$rows}else{$b+=$rows}}}
"{0} {1} merged: avg {2:N4} -> {3:N4}  p99 {4:N4} -> {5:N4}" -f $Set,$h,($b|Measure-Object -Average).Average,($c|Measure-Object -Average).Average,(P99 $b),(P99 $c)}
