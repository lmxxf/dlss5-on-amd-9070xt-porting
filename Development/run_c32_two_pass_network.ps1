param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
# NULL RESULT (kept off, see bench.ps1): fused C32 attention softmax in two passes -- pass 1 QK/exp/f16 row sums, pass 2 recomputes QK+exp and writes the
# normalized E4M3 P tile. One live score accumulator instead of four; the exps are recomputed bit for bit (identical output). Target: the
# 896-byte scratch spill of native_wave_c32_fused_attention_ffn (VGPR 128).
$env:DLSS5_BUILD_C32_TWO_PASS='1'
& "$Folder\run_c32_sat_cast_network.ps1" -Folder $Folder
exit $LASTEXITCODE
