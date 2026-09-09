param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH 3: C32 FFN input tile as accumulator loads + hardware E4M3 cast (no scalar staging), FP8 x FP8 expand.
# Host DLSS5_C32_FFN_FAST3 packs the E4M3 expand copy; kernel built with NATIVE_C32_FFN_FAST3 via DLSS5_BUILD_C32_FFN_FAST3.
$env:DLSS5_BUILD_C32_FFN_FAST3='1'
$env:DLSS5_C32_FFN_FAST3='1'
& "$Folder\run_tiled_weights_network.ps1" -Folder $Folder
exit $LASTEXITCODE
