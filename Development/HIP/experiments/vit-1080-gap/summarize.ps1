# ABBA summary with p99: slots 0/3 = base, 1/2 = candidate; frames >= 200 of 1000.
param([string[]]$Sets)
$root='D:\DLSSNR-Lab\hip-backend\vit-1080-gap-20261001'
$Sets=@($Sets|ForEach-Object{$_ -split ","}|Where-Object{$_})
function P99($v){$s=@($v|Sort-Object);$s[[math]::Ceiling(0.99*$s.Count)-1]}
foreach($set in $Sets){foreach($d in Get-ChildItem $root -Directory -Filter "runtime-regression-$set-timing-*"){foreach($h in 900,1080){
 $b=@();$c=@();foreach($slot in 0..3){$rows=@(Import-Csv "$($d.FullName)\time-$h-$slot\rgb.csv"|Where-Object{[int]$_.frame -ge 200}|ForEach-Object{[double]$_.wall_ms});if($slot -in 1,2){$c+=$rows}else{$b+=$rows}}
 $bm=($b|Measure-Object -Average).Average;$cm=($c|Measure-Object -Average).Average
 "{0} {1} {2}: avg {3:N4} -> {4:N4} ({5:+0.0000;-0.0000} ms, {6:+0.00;-0.00}%)  p99 {7:N4} -> {8:N4}" -f $set,$d.Name.Split('-')[-1],$h,$bm,$cm,($cm-$bm),(100*($cm-$bm)/$bm),(P99 $b),(P99 $c)}}}
