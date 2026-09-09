param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# Multihead attention stages Q/K/V through LDS with coalesced 128-byte reads.
# Refresh the manifest hash for the edited attention source before validation.
$Path=Join-Path $Folder 'shader-manifest.json'
$Manifest=Get-Content $Path -Raw | ConvertFrom-Json
$Entry=@($Manifest | Where-Object name -eq 'native_c64.hlsl')
if($Entry.Count -ne 1){throw 'Expected one native_c64.hlsl manifest entry'}
$Entry[0].sha256=(Get-FileHash (Join-Path $Folder 'native_c64.hlsl') -Algorithm SHA256).Hash
$Manifest | ConvertTo-Json | Set-Content $Path
$env:DLSS5_BUILD_COALESCED_QKV='1'
& "$Folder\run_blocked_vit_network.ps1" -Folder $Folder
exit $LASTEXITCODE
