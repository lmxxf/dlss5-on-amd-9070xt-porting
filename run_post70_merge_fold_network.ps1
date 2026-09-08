param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: post70 merge folded into the FFN gather (mapping mode 4: low-res main + skip + coefficients), no merged buffer pass.
$env:DLSS5_POST70_MERGE_FOLD='1'
& "$Folder\run_vit_attn_fp8_network.ps1" -Folder $Folder
exit $LASTEXITCODE
