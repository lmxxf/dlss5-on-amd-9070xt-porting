param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: chained C32 blocks (encoder 1-4, tail 66-69) read the previous block's raw tiles (mapping mode 3) so the
# predecessor's finish stage is skipped; residual = F(input)*scale via E4M3 diagonal MMAs. Needs C32 FFN fast3.
$env:DLSS5_C32_CHAIN_RAW='1'
& "$Folder\run_post70_direct_network.ps1" -Folder $Folder
exit $LASTEXITCODE
