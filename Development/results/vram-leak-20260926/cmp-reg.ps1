$old='D:\DLSSNR-Lab\hip-backend\vit-proj-n64-production\runtime-regression'
$new='D:\DLSSNR-Lab\hip-backend\vram-leak-regression\runtime-regression'
$same=0;$diff=0;$missing=0
foreach($d in Get-ChildItem $new -Directory){
  foreach($f in Get-ChildItem $d.FullName -File -Filter *.f16){
    $o=Join-Path (Join-Path $old $d.Name) $f.Name
    if(!(Test-Path $o)){$missing++;"MISSING old $($d.Name)\$($f.Name)";continue}
    if((Get-FileHash $o).Hash -eq (Get-FileHash $f.FullName).Hash){$same++}else{$diff++;"DIFF $($d.Name)\$($f.Name)"}
  }
}
"same=$same diff=$diff missing=$missing"
