param([switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$lab = 'D:\DLSSNR-Lab'
$source = Join-Path $lab 'Magpie-DLSS5-AMD-0.13'
$stage = Join-Path $lab 'Magpie-DLSS5-AMD-0.14'
$zip = "$stage.zip"
$addon = 'D:\Magpie-DLSS5\Magpie-Experimental-x64\Magpie-Experimental-x64\dlss5-amd.addon64'
$expected = 'B4AA401DE728288033A27B586E31DC0B1BB3EF0EE84385CB1CF894F611C49B8F'
if ((Get-FileHash $addon).Hash -ne $expected) { throw 'Installed addon differs from verified FPS build' }
if (!$VerifyOnly) {
if ((Test-Path $stage) -or (Test-Path $zip)) { throw '0.14 output already exists; inspect it before replacing' }
Copy-Item $source $stage -Recurse
Copy-Item $addon (Join-Path $stage 'dlss5-amd.addon64') -Force
Copy-Item (Join-Path $lab 'logs\README-magpie-0.14.txt') (Join-Path $stage 'README.txt') -Force
Copy-Item (Join-Path $lab 'logs\magpie-flags-0.14.txt') (Join-Path $stage 'DLSS5-AMD\native-game-flags.txt') -Force
Get-ChildItem $stage -Recurse -File | Where-Object { $_.Name -match '\.addon64\.|\.bak$|\.log$|^GpuDebuggingLog\.txt$|^SHA256SUMS(\.txt)?$|\.dmp$' } | Remove-Item -Force
Get-ChildItem $stage -Recurse -Directory | Where-Object { $_.Name -in @('logs','shader-cache') } | Sort-Object { $_.FullName.Length } -Descending | Remove-Item -Recurse -Force
$config = Get-Content (Join-Path $stage 'config\config.json') -Raw -Encoding UTF8
$null = $config | ConvertFrom-Json
if (!$config.Contains('XeSS_FrameGeneration_x2_ZeroMV')) { throw 'FG preset missing' }
$flags = Get-Content (Join-Path $stage 'DLSS5-AMD\native-game-flags.txt')
if ($flags -notcontains 'DLSS5_SHOW_FPS=1') { throw 'FPS flag missing' }
if ($flags -match '^DLSS5_(DEBUG_DUMPS|BLACK_PROBE)=1$') { throw 'Debug probes enabled' }
$utf8 = New-Object System.Text.UTF8Encoding($false)
$lines = @(Get-ChildItem $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
 (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_.FullName.Substring($stage.Length+1).Replace('\','/')
})
[IO.File]::WriteAllLines((Join-Path $stage 'SHA256SUMS.txt'),$lines,$utf8)
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip,[IO.Compression.CompressionLevel]::Fastest,$true)
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$utf8 = New-Object System.Text.UTF8Encoding($false)
$lines = [IO.File]::ReadAllLines((Join-Path $stage 'SHA256SUMS.txt'))
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try {
 $prefix = (Split-Path $stage -Leaf) + '/'
 $entries=@{};foreach($item in $archive.Entries){$entries[$item.FullName.Replace("\","/")]=$item}
 $verified=0
 foreach ($line in $lines) {
  $hash=$line.Substring(0,64);$name=$line.Substring(66)
  $entry=$entries[$prefix+$name]
  if (!$entry) { throw "Missing archive entry: $name" }
  $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
  try { $actual=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
  finally { $stream.Dispose();$sha.Dispose() }
  if ($actual -ne $hash) { throw "Archive hash mismatch: $name" }
  $verified++
 }
 "Verified archive files: $verified"
} finally { $archive.Dispose() }
$zipHash=(Get-FileHash $zip -Algorithm SHA256).Hash
[IO.File]::WriteAllText("$zip.sha256",$zipHash.ToLowerInvariant()+'  '+(Split-Path $zip -Leaf)+"`n",$utf8)
Get-Item $zip | Select-Object FullName,Length
"SHA256=$zipHash"
