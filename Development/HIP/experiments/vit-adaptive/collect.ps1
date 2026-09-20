param([string]$Tag='r1',[switch]$Rgb)
$ErrorActionPreference='Stop';Add-Type -AssemblyName System.IO.Compression;Add-Type -AssemblyName System.IO.Compression.FileSystem
$r='D:\DLSSNR-Lab\hip-backend';$work="$r\vit-adaptive-$Tag-results";$path="$r\vit-adaptive-$Tag.zip"
if(Test-Path $path){Remove-Item $path}
$archive=[IO.Compression.ZipFile]::Open($path,[IO.Compression.ZipArchiveMode]::Create)
try{foreach($file in Get-ChildItem $work -Recurse -File){
 if($file.Extension -notin '.csv','.log','.txt' -and !($Rgb -and ($file.Name -like 'rgb-frame-*.f16') -and ($file.Directory.Name -match '^(base|adaptive)-[0-9]+-s[1-7]$'))){continue}
 $rel=$file.FullName.Substring($work.Length+1).Replace('\','/');$entry=$archive.CreateEntry($rel,[IO.Compression.CompressionLevel]::Fastest);$dst=$entry.Open();$src=[IO.File]::OpenRead($file.FullName);try{$src.CopyTo($dst)}finally{$src.Dispose();$dst.Dispose()}
}}finally{$archive.Dispose()}
Get-Item $path|Select-Object Name,Length
