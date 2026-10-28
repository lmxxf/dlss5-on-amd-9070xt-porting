$ErrorActionPreference='Stop'
function SetIni($text,$section,$key,$value){
 $pattern='(?ms)^\['+[regex]::Escape($section)+'\]\r?\n.*?(?=^\[|\z)'
 $m=[regex]::Match($text,$pattern)
 if(!$m.Success){return $text+"`r`n[$section]`r`n$key=$value`r`n"}
 $part=$m.Value;$kp='(?m)^'+[regex]::Escape($key)+'=[^\r\n]*'
 if([regex]::IsMatch($part,$kp)){$part=[regex]::Replace($part,$kp,"$key=$value")}else{$part=$part.TrimEnd()+"`r`n$key=$value`r`n"}
 return $text.Substring(0,$m.Index)+$part+$text.Substring($m.Index+$m.Length)
}
$cases=@("[Inputs]`nEnableFfxInputs=true`n", "[DlssNr]`nTransferStrength=1.5`nColourStrength=1.1`n[Other]`nKeep=yes`n")
foreach($text in $cases){
 $section=''
 foreach($line in Get-Content 'D:\DLSSNR-Lab\backfeed-20261028\optiscaler-regular.ini'){
  if($line -match '^\[(.+)\]'){$section=$matches[1]}elseif($line -match '^([^;#][^=]*)=(.*)$'){$text=SetIni $text $section $matches[1] $matches[2]}
 }
 foreach($key in @('TransferStrength','ColourStrength')){if($text -notmatch ('(?m)^'+$key+'=1\.0\s*$')){throw "ini key mismatch $key"}}
 if($text -match '1\.5|1\.1'){throw 'old strength survived'}
 Write-Output $text
}
'INI_MERGE_PASS'
