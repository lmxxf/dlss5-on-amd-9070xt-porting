$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend';$out="$r\vit-adaptive-r3-quality-results";$rows=@()
foreach($s in 1..7){foreach($kind in 'base','adaptive'){foreach($f in 0..11){
 $old="$r\vit-adaptive-r2-results\$kind-900-s$s\rgb-frame-$f.f16";$new="$out\$kind-900-s$s\rgb-frame-$f.f16"
 $a=(Get-FileHash $old).Hash;$b=(Get-FileHash $new).Hash
 $rows += [pscustomobject]@{sequence=$s;kind=$kind;frame=$f;r2_sha=$a;r3_sha=$b;identical=($a -eq $b)}
}}}
$rows|Export-Csv "$out\r2-r3-identity.csv" -NoTypeInformation
$bad=@($rows|Where-Object{!$_.identical});Write-Output "Compared $($rows.Count) R2/R3 frames; differences=$($bad.Count)"
if($bad.Count){$bad|Select-Object sequence,kind,frame;throw 'R3 differs: collect RGB and measure separately'}
