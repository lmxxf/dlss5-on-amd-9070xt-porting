param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# FAST PATH: the C32 finish (main8 + pooled down) and the post70 rgb head run in the attention epilogue; pre/post/blocks 4,69 no longer write raw. Exact.
$env:DLSS5_BUILD_C32_EPILOGUE='1'
$env:DLSS5_C32_EPILOGUE='1'
& "$Folder\run_c32_half_stream_network.ps1" -Folder $Folder
exit $LASTEXITCODE
