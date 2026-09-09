param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: preblock input-mix prefix as float dot + f16 round (cs_5_1 shader compiled at runtime; manifest hash updated by update-manifest.ps1).
$env:DLSS5_FAST_PREFIX='1'
& "$Folder\run_fp8_qkv_norm_network.ps1" -Folder $Folder
exit $LASTEXITCODE
