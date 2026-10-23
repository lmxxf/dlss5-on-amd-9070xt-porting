param([Parameter(Mandatory=$true)][string]$ModulePath,[Parameter(Mandatory=$true)][string]$ProbePath,[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
# Read-only CPU verification after the root's guarded GPU invocation. This script launches nothing.
$r=Get-Content (Join-Path $OutputDirectory 'receipt.json') -Raw|ConvertFrom-Json
if(!$r.pass -or $r.sticky_status -ne 1 -or $r.wait_limit -ne 1 -or $r.producer_launches -ne 0 -or $r.downstream_launches -ne 0){throw 'Probe receipt failed'}
$guard=256
foreach($spec in @(@{name='float-output-with-canaries.bin';payload=32768;canary=165},@{name='byte-output-with-canaries.bin';payload=8192;canary=90})){
 $b=[IO.File]::ReadAllBytes((Join-Path $OutputDirectory $spec.name))
 if($b.Length -ne 2*$guard+$spec.payload){throw "wrong output extent $($spec.name)"}
 for($i=0;$i -lt $b.Length;$i++){$want=if($i -ge $guard -and $i -lt $guard+$spec.payload){0}else{$spec.canary};if($b[$i] -ne $want){throw "output/guard mismatch $($spec.name) offset $i"}}
}
$ready=[IO.File]::ReadAllBytes((Join-Path $OutputDirectory 'ready-with-canaries.bin'))
$status=[IO.File]::ReadAllBytes((Join-Path $OutputDirectory 'status-with-canaries.bin'))
if($ready.Length -ne 520 -or $status.Length -ne 516){throw 'control extent'}
for($i=0;$i -lt $ready.Length;$i++){$want=if($i -ge 256 -and $i -lt 264){0}else{195};if($ready[$i] -ne $want){throw 'ready changed'}}
for($i=0;$i -lt $status.Length;$i++){$want=if($i -eq 256){1}elseif($i -ge 257 -and $i -lt 260){0}else{212};if($status[$i] -ne $want){throw 'status/canary mismatch'}}
$files=@($ModulePath,$ProbePath,(Join-Path $OutputDirectory 'receipt.json'))+@(Get-ChildItem $OutputDirectory -Filter '*.bin'|ForEach-Object {$_.FullName})
$hashes=@($files|ForEach-Object {@{path=$_;bytes=(Get-Item $_).Length;sha256=(Get-FileHash $_ -Algorithm SHA256).Hash.ToLower()}})
@{cpu_raw_check=$true;root_guarded_gpu_receipt=$r;files=$hashes}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $OutputDirectory 'collected.json')
'TIMEOUT_CPU_READBACK_CHECK_PASS'
