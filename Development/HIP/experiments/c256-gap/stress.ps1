# SP_ENDS timeout fault injection (layer 0 = block 15 strands its children -> ~100ms timeout -> GPU serial recovery), sync + async, vs base
param([string]$Set='ENDS')
$ErrorActionPreference='Stop';$root='D:\DLSSNR-Lab\hip-backend\c256-gap-20261001'
& "$root\lock.ps1" take
try{
$env:SP_TICKET_LIMIT='4294967295';$env:SP_TICKET_START='0';$env:SP_FORCE_TIMEOUT='1';$env:SP_VALIDATE='1';$env:SP_TRACE='1'
& "$root\regression.ps1" -Set $Set -BenchName benchmark-base.exe -CandidateBenchName benchmark-Proll.exe -CorrectnessOnly -Only @('900-motion','1080-motion') -Batch "timeout-sync"
$env:SP_VALIDATE='0';$env:SP_TRACE='0'
& "$root\regression.ps1" -Set $Set -BenchName benchmark-base.exe -CandidateBenchName benchmark-Proll.exe -CorrectnessOnly -Only @('900-history','1080-history') -Batch "timeout-async"
}finally{$env:SP_FORCE_TIMEOUT='0';& "$root\lock.ps1" drop}
'STRESS_DONE'
