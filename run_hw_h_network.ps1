param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: software H() (f16 RNE) replaced by the hardware f32tof16/f16tof32 pair in the fast kernels.
$env:DLSS5_BUILD_HW_H='1'
& "$Folder\run_c32_ffn_fp8_network.ps1" -Folder $Folder
exit $LASTEXITCODE
