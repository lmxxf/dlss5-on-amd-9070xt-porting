param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH stage 3a: activation epilogue without intermediate f16 roundings (on top of FP8 operands + hardware accumulation).
$env:DLSS5_FAST_EPILOGUE='1'
& "$Folder\run_fast_fp8_hidden_network.ps1" -Folder $Folder -Lds
exit $LASTEXITCODE
