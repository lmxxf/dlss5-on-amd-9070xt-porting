param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# C32 attention reads packed f16 weights directly with wave loads instead of staging them into LDS per window.
$Path=Join-Path $Folder 'shader-manifest.json'
$Manifest=Get-Content $Path -Raw | ConvertFrom-Json
$Entry=@($Manifest | Where-Object name -eq 'preblock_attention_four_wave.hlsl')
if($Entry.Count -eq 1){$Entry[0].sha256=(Get-FileHash (Join-Path $Folder 'preblock_attention_four_wave.hlsl') -Algorithm SHA256).Hash;$Manifest | ConvertTo-Json | Set-Content $Path}
$env:DLSS5_BUILD_C32_LOCAL_WEIGHTS='1'
$env:DLSS5_TEST_LOCAL_C32_ATTENTION='1'
& "$Folder\run_wave_project_network.ps1" -Folder $Folder
exit $LASTEXITCODE
