param([string]$Prefix='capture',[string]$Tag='', [switch]$RgbOnly)
$ErrorActionPreference='Stop';Add-Type -AssemblyName System.IO.Compression;Add-Type -AssemblyName System.IO.Compression.FileSystem
$r='D:\DLSSNR-Lab\hip-backend';$work="$r\vit-residual$Tag-results";$path="$r\vit-residual$Tag-$Prefix.zip"
if(Test-Path $path){Remove-Item $path}
$archive=[IO.Compression.ZipFile]::Open($path,[IO.Compression.ZipArchiveMode]::Create)
try{foreach($dir in Get-ChildItem $work -Directory -Filter "$Prefix-900-s*"){foreach($file in Get-ChildItem $dir.FullName -Recurse -File){
 if($RgbOnly -and $file.Extension -eq '.f32'){continue}
 if($file.Extension -notin '.f16','.f32','.csv','.log'){continue}
 $rel=$file.FullName.Substring($work.Length+1).Replace('\','/');$entry=$archive.CreateEntry($rel,[IO.Compression.CompressionLevel]::Fastest);$dst=$entry.Open();$src=[IO.File]::OpenRead($file.FullName);try{$src.CopyTo($dst)}finally{$src.Dispose();$dst.Dispose()}
}}}finally{$archive.Dispose()}
Get-Item $path|Select-Object Name,Length
