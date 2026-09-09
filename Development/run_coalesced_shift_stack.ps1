param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# Re-evaluate the coalesced shift pack/crop copies on top of the resident-weights stack.
$Path=Join-Path $Folder 'shader-manifest.json'
$Manifest=Get-Content $Path -Raw | ConvertFrom-Json
$Entry=@($Manifest | Where-Object name -eq 'native_c64_shift.hlsl')
if($Entry.Count -ne 1){throw 'Expected one native_c64_shift.hlsl manifest entry'}
$Entry[0].sha256=(Get-FileHash (Join-Path $Folder 'native_c64_shift.hlsl') -Algorithm SHA256).Hash
$Manifest | ConvertTo-Json | Set-Content $Path
$env:DLSS5_TEST_COALESCED_MULTIHEAD_SHIFT='1'
& "$Folder\run_resident_weights_network.ps1" -Folder $Folder
exit $LASTEXITCODE
