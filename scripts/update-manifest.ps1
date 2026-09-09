param([Parameter(Mandatory=$true)][string]$Folder,[Parameter(Mandatory=$true)][string[]]$Names)
$ErrorActionPreference='Stop'
$Path=Join-Path $Folder 'shader-manifest.json'
$Manifest=Get-Content $Path -Raw | ConvertFrom-Json
foreach($Name in $Names){
 $Entry=@($Manifest | Where-Object name -eq $Name)
 if($Entry.Count -ne 1){throw "Expected one shader entry: $Name"}
 $Entry[0].sha256=(Get-FileHash (Join-Path $Folder $Name) -Algorithm SHA256).Hash
}
$Manifest | ConvertTo-Json | Set-Content $Path
